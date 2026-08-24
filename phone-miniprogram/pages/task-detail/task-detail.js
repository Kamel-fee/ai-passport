const app = getApp();
const ble = require('../../utils/ble.js');

Page({
  data: {
    task: null,
    taskId: -1,
    bleConnected: false
  },

  onLoad(options) {
    const taskId = parseInt(options.id);
    const task = app.getTask(taskId);
    if (task) {
      const displayTask = {
        ...task,
        totalDuration: task.nodes.reduce((sum, n) => sum + (n.durationSec || 60), 0),
        totalDurationStr: this.formatDuration(task.nodes.reduce((sum, n) => sum + (n.durationSec || 60), 0))
      };
      this.setData({ task: displayTask, taskId });
    }
  },

  onShow() {
    this.setData({ bleConnected: ble.isConnected() });
  },

  formatDuration(sec) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
    return `${m}:${String(s).padStart(2, '0')}`;
  },

  onEdit() {
    wx.navigateTo({
      url: `/pages/task-edit/task-edit?id=${this.data.taskId}`
    });
  },

  onSendToCard() {
    if (!ble.isConnected()) {
      wx.showToast({ title: '请先连接卡片', icon: 'none' });
      return;
    }
    wx.showLoading({ title: '发送中...' });
    ble.sendTask(this.data.task).then(() => {
      wx.hideLoading();
      wx.showToast({ title: '已发送到卡片', icon: 'success' });
    }).catch(err => {
      wx.hideLoading();
      wx.showToast({ title: '发送失败', icon: 'none' });
    });
  },

  onDelete() {
    wx.showModal({
      title: '删除任务',
      content: '确定要删除这个任务吗？',
      success: (res) => {
        if (res.confirm) {
          app.deleteTask(this.data.taskId);
          wx.navigateBack();
        }
      }
    });
  }
});
