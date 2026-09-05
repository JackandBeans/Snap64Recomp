import AppKit
import Observation

struct ControllerPreferences: Codable, Equatable {
    var deadzone = 0.12
    var sensitivity = 1.0
    var invertY = false
    var dpadNavigation = true
    var rightStickAim = true
    var rumble = true
}
struct ControllerStatus: Decodable {
    var connected = false
    var name = "No controller connected"
    var dualSense = false
    var canRumble = false
    var buttons = [Bool](repeating: false, count: 21)
    var axes = [Double](repeating: 0, count: 6)
    var mappedX = 0.0
    var mappedY = 0.0
    var options = ControllerPreferences()
}
typealias ReadController = @convention(c) () -> UnsafePointer<CChar>?
typealias SaveController = @convention(c) (UnsafePointer<CChar>?) -> Int32
typealias TestController = @convention(c) () -> Int32
typealias FocusController = @convention(c) (Int32) -> Void

@Observable final class ControllerStore {
    var status = ControllerStatus()
    var options = ControllerPreferences()
    var message = "Changes apply immediately."
    private var loaded = false
    var read: ReadController?
    var save: SaveController?
    var test: TestController?
    func refresh() {
        guard let text = read?(), let value = try? JSONDecoder().decode(ControllerStatus.self, from: Data(String(cString: text).utf8)) else { return }
        status = value
        if !loaded { options=value.options;loaded=true }
    }
    func persist() {
        guard let data=try? JSONEncoder().encode(options), let text=String(data:data,encoding:.utf8) else { return }
        let ok=text.withCString { save?($0) == 1 }
        message=ok ? "Saved · applies immediately" : "Could not save controller settings."
    }
    func restore() {options=ControllerPreferences();persist()}
    func testRumble() {message=test?() == 1 ? "Vibration sent to the controller." : "Vibration unavailable. Connect a controller and enable vibration."}
    func bluetooth() {
        guard let url=URL(string:"x-apple.systempreferences:com.apple.BluetoothSettings") else {return}
        NSWorkspace.shared.open(url)
    }
}
