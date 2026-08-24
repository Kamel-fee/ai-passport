const app = getApp();
const ble = require('../../utils/ble.js');

Page({
  data: {
    history: [],
    bleConnected: false
  },

  onLoad() {
    this.refreshHistory();
  },

  onShow() {
    this.refreshHistory();
    this.setData({ bleConnected: ble.isConnected() });
  },

  refreshHistory() {
    const history = app.globalData.history.map((record, index) => ({
      ...record,
      id: index,
      completedOnTime: record.completedOnTime,
      timeStr: this.formatTime(record.timestamp),
      plannedStr: this.formatDuration(record.totalSec),
      actualStr: this.formatDuration(record.actualSec),
      diffStr: this.formatDuration(Math.abs(record.actualSec - record.totalSec))
    }));
    this.setData({ history });
  },

  formatTime(ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    const m = String(d.getMinutes()).padStart(2, '0');
    const s = String(d.getSeconds()).padStart(2, '0');
    const h = String(d.getHours()).padStart(2, '0');
    const day = String(d.getDate()).padStart(2, '0');
    const month = String(d.getMonth() + 1).padStart(2, '0');
    return `${month}-${day} ${h}:${m}:${s}`;
  },

  formatDuration(sec) {
    if (!sec) return '0秒';
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}时${m}分${s}秒`;
    if (m > 0) return `${m}分${s}秒`;
    return `${s}秒`;
  },

  onFetchFromCard() {
    if (!ble.isConnected()) {
      wx.showToast({ title: '请先连接卡片', icon: 'none' });
      return;
    }
    wx.showLoading({ title: '读取中...' });
    ble.requestHistory().then(() => {
      setTimeout(() => {
        wx.hideLoading();
        wx.showToast({ title: '已请求', icon: 'success' });
      }, 1000);
    }).catch(() => {
      wx.hideLoading();
      wx.showToast({ title: '读取失败', icon: 'none' });
    });
  },

  onClearHistory() {
    wx.showModal({
      title: '清空历史',
      content: '确定要清空所有历史记录吗？',
      success: (res) => {
        if (res.confirm) {
          app.saveHistory([]);
          this.refreshHistory();
          wx.showToast({ title: '已清空', icon: 'success' });
        }
      }
    });
  }
});
