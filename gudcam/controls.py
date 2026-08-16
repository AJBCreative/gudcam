class ControlManager:
    def __init__(self, engine):
        self.engine = engine
        self.controls = []
        self.control_map = {}

    def refresh(self):
        self.controls = self.engine.get_controls()
        self.control_map = {c['id']: c for c in self.controls}
        return self.controls

    def update_val(self, ctrl_id, val):
        if ctrl_id in self.control_map:
            self.control_map[ctrl_id]['value'] = val
        return self.engine.set_control(ctrl_id, int(val))
