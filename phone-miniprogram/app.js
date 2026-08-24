App({
  globalData: {
    bleConnected: false,
    deviceId: null,
    tasks: [],
    history: []
  },

  onLaunch() {
    this.loadTasks();
    this.loadHistory();
  },

  loadTasks() {
    const tasks = wx.getStorageSync('tasks') || [];
    this.globalData.tasks = tasks;
    return tasks;
  },

  saveTasks(tasks) {
    this.globalData.tasks = tasks;
    wx.setStorageSync('tasks', tasks);
  },

  loadHistory() {
    const history = wx.getStorageSync('history') || [];
    this.globalData.history = history;
    return history;
  },

  saveHistory(history) {
    this.globalData.history = history;
    wx.setStorageSync('history', history);
  },

  addTask(task) {
    const tasks = this.globalData.tasks;
    tasks.push(task);
    this.saveTasks(tasks);
  },

  updateTask(index, task) {
    const tasks = this.globalData.tasks;
    if (index >= 0 && index < tasks.length) {
      tasks[index] = task;
      this.saveTasks(tasks);
    }
  },

  deleteTask(index) {
    const tasks = this.globalData.tasks;
    if (index >= 0 && index < tasks.length) {
      tasks.splice(index, 1);
      this.saveTasks(tasks);
    }
  },

  getTask(index) {
    return this.globalData.tasks[index];
  }
});
