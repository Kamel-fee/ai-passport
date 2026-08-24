const app = getApp();

Page({
  data: {
    isEdit: false,
    task: {
      name: '',
      nodes: [
        { name: '节点1', durationSec: 60 },
        { name: '节点2', durationSec: 120 }
      ]
    },
    nodeCount: 2,
    totalDuration: 180,
    timePresets: [
      { label: '30秒', value: 30 },
      { label: '1分钟', value: 60 },
      { label: '3分钟', value: 180 },
      { label: '5分钟', value: 300 },
      { label: '10分钟', value: 600 },
      { label: '25分钟', value: 1500 }
    ]
  },

  onLoad(options) {
    if (options.id !== undefined) {
      const task = app.getTask(parseInt(options.id));
      if (task) {
        this.setData({
          isEdit: true,
          task: JSON.parse(JSON.stringify(task)),
          nodeCount: task.nodes.length
        });
        this.calcTotal();
      }
    }
  },

  onTaskNameInput(e) {
    this.setData({ 'task.name': e.detail.value });
  },

  onNodeNameInput(e) {
    const idx = e.currentTarget.dataset.idx;
    this.setData({ [`task.nodes[${idx}].name`]: e.detail.value });
  },

  onNodeDurationInput(e) {
    const idx = e.currentTarget.dataset.idx;
    const val = parseInt(e.detail.value) || 0;
    this.setData({ [`task.nodes[${idx}].durationSec`]: val });
    this.calcTotal();
  },

  onPresetTap(e) {
    const idx = e.currentTarget.dataset.idx;
    const val = e.currentTarget.dataset.val;
    this.setData({ [`task.nodes[${idx}].durationSec`]: val });
    this.calcTotal();
  },

  calcTotal() {
    const total = this.data.task.nodes.reduce((sum, n) => sum + (parseInt(n.durationSec) || 0), 0);
    this.setData({ totalDuration: total });
  },

  onAddNode() {
    if (this.data.task.nodes.length >= 8) {
      wx.showToast({ title: '最多8个节点', icon: 'none' });
      return;
    }
    const nodes = [...this.data.task.nodes];
    nodes.push({
      name: `节点${nodes.length + 1}`,
      durationSec: 60
    });
    this.setData({
      'task.nodes': nodes,
      nodeCount: nodes.length
    });
    this.calcTotal();
  },

  onRemoveNode(e) {
    const idx = e.currentTarget.dataset.idx;
    if (this.data.task.nodes.length <= 1) {
      wx.showToast({ title: '至少保留1个节点', icon: 'none' });
      return;
    }
    const nodes = this.data.task.nodes.filter((_, i) => i !== idx);
    nodes.forEach((n, i) => {
      n.name = n.name.replace(/节点\d+/, `节点${i + 1}`);
    });
    this.setData({
      'task.nodes': nodes,
      nodeCount: nodes.length
    });
    this.calcTotal();
  },

  onSave() {
    const task = this.data.task;
    if (!task.name.trim()) {
      wx.showToast({ title: '请输入任务名称', icon: 'none' });
      return;
    }
    for (let i = 0; i < task.nodes.length; i++) {
      if (!task.nodes[i].name.trim()) {
        wx.showToast({ title: `请输入节点${i + 1}名称`, icon: 'none' });
        return;
      }
      if (!task.nodes[i].durationSec || task.nodes[i].durationSec <= 0) {
        wx.showToast({ title: `节点${i + 1}时间必须大于0`, icon: 'none' });
        return;
      }
    }

    if (this.data.isEdit) {
      app.updateTask(this.options.id, task);
      wx.showToast({ title: '已保存', icon: 'success' });
    } else {
      app.addTask(task);
      wx.showToast({ title: '已创建', icon: 'success' });
    }
    setTimeout(() => wx.navigateBack(), 500);
  },

  onAutoSegment() {
    const totalMin = Math.ceil(this.data.totalDuration / 60);
    const segmentCount = Math.min(Math.max(totalMin, 2), 8);
    const perSegment = Math.floor(this.data.totalDuration / segmentCount);
    const remainder = this.data.totalDuration % segmentCount;

    const nodes = [];
    for (let i = 0; i < segmentCount; i++) {
      nodes.push({
        name: `第${i + 1}段`,
        durationSec: perSegment + (i < remainder ? 1 : 0)
      });
    }
    this.setData({
      'task.nodes': nodes,
      nodeCount: segmentCount
    });
    this.calcTotal();
    wx.showToast({ title: '已自动分段', icon: 'success' });
  },

  onDistributeEven() {
    const nodes = this.data.task.nodes;
    if (nodes.length === 0) return;
    const perNode = Math.floor(this.data.totalDuration / nodes.length);
    const remainder = this.data.totalDuration % nodes.length;
    const updated = nodes.map((n, i) => ({
      ...n,
      durationSec: perNode + (i < remainder ? 1 : 0)
    }));
    this.setData({ 'task.nodes': updated });
    this.calcTotal();
  }
});
