const ble = require('../../utils/ble.js');
const app = getApp();

Page({
  data: {
    tasks: [],
    bleConnected: false,
    bleStatus: '未连接',
    showScanModal: false,
    discoveredDevices: [],
    scanning: false
  },

  onLoad() {
    this.refreshTasks();
  },

  onShow() {
    this.refreshTasks();
    this.setData({
      bleConnected: ble.isConnected(),
      bleStatus: ble.isConnected() ? '已连接' : '未连接'
    });
  },

  refreshTasks() {
    const tasks = app.globalData.tasks.map((task, index) => ({
      ...task,
      id: index,
      totalDuration: task.nodes.reduce((sum, n) => sum + (n.durationSec || 60), 0),
      nodeCount: task.nodes.length,
      totalDurationStr: this.formatDuration(task.nodes.reduce((sum, n) => sum + (n.durationSec || 60), 0))
    }));
    this.setData({ tasks });
  },

  formatDuration(sec) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
    return `${m}:${String(s).padStart(2, '0')}`;
  },

  onTaskTap(e) {
    const id = e.currentTarget.dataset.id;
    wx.navigateTo({
      url: `/pages/task-detail/task-detail?id=${id}`
    });
  },

  onTaskEdit(e) {
    const id = e.currentTarget.dataset.id;
    wx.navigateTo({
      url: `/pages/task-edit/task-edit?id=${id}`
    });
  },

  onTaskDelete(e) {
    const id = e.currentTarget.dataset.id;
    wx.showModal({
      title: '删除任务',
      content: '确定要删除这个任务吗？',
      success: (res) => {
        if (res.confirm) {
          app.deleteTask(id);
          this.refreshTasks();
          wx.showToast({ title: '已删除', icon: 'success' });
        }
      }
    });
  },

  onAddTask() {
    wx.navigateTo({
      url: '/pages/task-edit/task-edit'
    });
  },

  onConnectBLE() {
    if (ble.isConnected()) {
      wx.showActionSheet({
        itemList: ['断开连接', '同步任务到卡片', '从卡片读取历史'],
        success: (res) => {
          if (res.tapIndex === 0) {
            ble.disconnect().then(() => {
              this.setData({
                bleConnected: false,
                bleStatus: '未连接'
              });
              wx.showToast({ title: '已断开', icon: 'success' });
            });
          } else if (res.tapIndex === 1) {
            this.syncToCard();
          } else if (res.tapIndex === 2) {
            this.readHistory();
          }
        }
      });
    } else {
      this.setData({ showScanModal: true });
      this.startScan();
    }
  },

  startScan() {
    this.setData({ scanning: true, discoveredDevices: [] });
    ble.scanDevices().then(devices => {
      this.setData({
        discoveredDevices: devices,
        scanning: false
      });
      if (devices.length === 0) {
        wx.showToast({ title: '未发现设备', icon: 'none' });
      }
    }).catch(err => {
      this.setData({ scanning: false });
      wx.showToast({ title: '扫描失败', icon: 'none' });
    });
  },

  onDeviceTap(e) {
    const deviceId = e.currentTarget.dataset.id;
    wx.showLoading({ title: '连接中...' });
    ble.connect(deviceId).then(() => {
      wx.hideLoading();
      this.setData({
        bleConnected: true,
        bleStatus: '已连接',
        showScanModal: false
      });
      wx.showToast({ title: '已连接', icon: 'success' });
    }).catch(err => {
      wx.hideLoading();
      wx.showToast({ title: '连接失败', icon: 'none' });
    });
  },

  closeScanModal() {
    this.setData({ showScanModal: false });
  },

  syncToCard() {
    if (this.data.tasks.length === 0) {
      wx.showToast({ title: '没有任务可同步', icon: 'none' });
      return;
    }
    wx.showLoading({ title: '同步中...' });
    const tasks = this.data.tasks;
    let idx = 0;

    const sendNext = () => {
      if (idx >= tasks.length) {
        wx.hideLoading();
        wx.showToast({ title: '同步完成', icon: 'success' });
        return;
      }
      ble.sendTask(tasks[idx]).then(() => {
        idx++;
        setTimeout(sendNext, 100);
      }).catch(err => {
        wx.hideLoading();
        wx.showToast({ title: '同步失败', icon: 'none' });
      });
    };
    sendNext();
  },

  readHistory() {
    wx.showLoading({ title: '读取中...' });
    ble.requestHistory().then(() => {
      setTimeout(() => {
        ble.readCharacteristic(ble.CHAR_HISTORY_READ).then(res => {
          wx.hideLoading();
          wx.showToast({ title: '已读取', icon: 'success' });
        }).catch(() => {
          wx.hideLoading();
        });
      }, 500);
    }).catch(() => {
      wx.hideLoading();
      wx.showToast({ title: '读取失败', icon: 'none' });
    });
  },

  onHistoryTap() {
    wx.navigateTo({
      url: '/pages/history/history'
    });
  }
});
