const SERVICE_UUID = '0000FFE0-0000-1000-8000-00805F9B34FB';
const CHAR_TASK_WRITE = '0000FFE1-0000-1000-8000-00805F9B34FB';
const CHAR_TASK_READ = '0000FFE2-0000-1000-8000-00805F9B34FB';
const CHAR_HISTORY_READ = '0000FFE3-0000-1000-8000-00805F9B34FB';
const CHAR_CMD_WRITE = '0000FFE4-0000-1000-8000-00805F9B34FB';

let deviceId = null;
let connected = false;

function isConnected() {
  return connected;
}

function getDeviceId() {
  return deviceId;
}

function scanDevices() {
  return new Promise((resolve, reject) => {
    wx.openBluetoothAdapter({
      success: () => {
        wx.startBluetoothDevicesDiscovery({
          allowDuplicatesKey: false,
          success: () => {
            const devices = [];
            const foundIds = {};
            const onDeviceFound = (res) => {
              res.devices.forEach(device => {
                // 安卓部分机型 name 为空,名字在 localName 里
                const name = device.name || device.localName || '';
                if (name.indexOf('FoloToy') !== -1 && !foundIds[device.deviceId]) {
                  foundIds[device.deviceId] = true;
                  device.name = name;
                  devices.push(device);
                }
              });
            };
            wx.onBluetoothDeviceFound(onDeviceFound);

            setTimeout(() => {
              wx.stopBluetoothDevicesDiscovery();
              wx.offBluetoothDeviceFound();
              resolve(devices);
            }, 5000);
          },
          fail: reject
        });
      },
      fail: (err) => {
        // 蓝牙开关未打开/无权限时给出明确提示
        reject(err);
      }
    });
  });
}

function connect(deviceIdArg) {
  return new Promise((resolve, reject) => {
    wx.createBLEConnection({
      deviceId: deviceIdArg,
      success: () => {
        deviceId = deviceIdArg;
        connected = true;
        resolve();
      },
      fail: reject
    });
  });
}

function disconnect() {
  return new Promise((resolve, reject) => {
    if (deviceId) {
      wx.closeBLEConnection({
        deviceId: deviceId,
        success: () => {
          connected = false;
          deviceId = null;
          resolve();
        },
        fail: reject
      });
    } else {
      resolve();
    }
  });
}

function discoverServices() {
  return new Promise((resolve, reject) => {
    if (!deviceId) { reject(new Error('Not connected')); return; }
    wx.getBLEDeviceServices({
      deviceId: deviceId,
      success: (res) => {
        resolve(res.services);
      },
      fail: reject
    });
  });
}

function discoverCharacteristics(serviceUuid) {
  return new Promise((resolve, reject) => {
    if (!deviceId) { reject(new Error('Not connected')); return; }
    wx.getBLEDeviceCharacteristics({
      deviceId: deviceId,
      serviceId: serviceUuid,
      success: (res) => {
        resolve(res.characteristics);
      },
      fail: reject
    });
  });
}

function writeCharacteristic(characteristicUuid, value) {
  return new Promise((resolve, reject) => {
    if (!deviceId) { reject(new Error('Not connected')); return; }
    wx.writeBLECharacteristicValue({
      deviceId: deviceId,
      serviceId: SERVICE_UUID,
      characteristicId: characteristicUuid,
      value: value,
      success: resolve,
      fail: reject
    });
  });
}

function readCharacteristic(characteristicUuid) {
  return new Promise((resolve, reject) => {
    if (!deviceId) { reject(new Error('Not connected')); return; }
    wx.readBLECharacteristicValue({
      deviceId: deviceId,
      serviceId: SERVICE_UUID,
      characteristicId: characteristicUuid,
      success: resolve,
      fail: reject
    });
  });
}

function sendTask(task) {
  const buffer = new ArrayBuffer(1024);
  const view = new DataView(buffer);
  let offset = 0;

  const nameBytes = new TextEncoder().encode(task.name.substring(0, 31));
  for (let i = 0; i < 32; i++) {
    view.setUint8(offset, i < nameBytes.length ? nameBytes[i] : 0);
    offset++;
  }

  view.setUint8(offset, task.nodes.length);
  offset++;

  task.nodes.forEach(node => {
    const nodeNameBytes = new TextEncoder().encode(node.name.substring(0, 15));
    for (let i = 0; i < 16; i++) {
      view.setUint8(offset, i < nodeNameBytes.length ? nodeNameBytes[i] : 0);
      offset++;
    }
    view.setUint32(offset, node.durationSec || 60, true);
    offset += 4;
  });

  const data = new Uint8Array(buffer, 0, offset);
  return writeCharacteristic(CHAR_TASK_WRITE, data.buffer);
}

function requestTaskList() {
  const cmd = new Uint8Array([0x01]);
  return writeCharacteristic(CHAR_CMD_WRITE, cmd.buffer);
}

function requestHistory() {
  const cmd = new Uint8Array([0x02]);
  return writeCharacteristic(CHAR_CMD_WRITE, cmd.buffer);
}

function clearHistory() {
  const cmd = new Uint8Array([0x03]);
  return writeCharacteristic(CHAR_CMD_WRITE, cmd.buffer);
}

function onCharacteristicChange(callback) {
  if (!deviceId) return;
  wx.onBLECharacteristicValueChange(res => {
    callback(res.characteristicId, res.value);
  });
}

module.exports = {
  SERVICE_UUID,
  CHAR_TASK_WRITE,
  CHAR_TASK_READ,
  CHAR_HISTORY_READ,
  CHAR_CMD_WRITE,
  isConnected,
  getDeviceId,
  scanDevices,
  connect,
  disconnect,
  discoverServices,
  discoverCharacteristics,
  writeCharacteristic,
  readCharacteristic,
  sendTask,
  requestTaskList,
  requestHistory,
  clearHistory,
  onCharacteristicChange
};
