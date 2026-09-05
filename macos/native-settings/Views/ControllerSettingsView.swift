import SwiftUI

struct ControllerSettingsView: View {
    @Bindable var store: ControllerStore
    var body: some View {
        VStack(spacing:0) {
            HStack(spacing:14) {
                Image(systemName:"gamecontroller.fill").font(.system(size:34)).foregroundStyle(.tint)
                VStack(alignment:.leading,spacing:4) {
                    Text("DualSense Controller").font(.title2.bold())
                    HStack(spacing:6) {
                        Circle().fill(store.status.connected ? .green : .secondary).frame(width:7,height:7)
                        Text(store.status.name).font(.callout).foregroundStyle(.secondary)
                    }
                }
                Spacer()
                Button("Bluetooth…",action:store.bluetooth)
            }.padding(22)
            TabView {
                controls.tabItem {Label("Controls",systemImage:"slider.horizontal.3")}
                ControllerMappingView().tabItem {Label("Button Map",systemImage:"gamecontroller")}
                ControllerTestView(store:store).tabItem {Label("Input Test",systemImage:"waveform.path")}
                pairing.tabItem {Label("Pairing",systemImage:"antenna.radiowaves.left.and.right")}
            }.padding(.horizontal,16)
            HStack {
                Text(store.message).font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("Restore Defaults",action:store.restore)
            }.padding(18)
        }
        .frame(width:660,height:650)
        .onChange(of:store.options) {_,_ in store.persist()}
    }
    private var controls: some View {
        Form {
            Section {
                Text("Hold L2 to aim, then press R2 to take a photo.").font(.headline)
                Text("Cross confirms and throws food; Circle cancels and throws Pester Balls. Items and the Dash Engine become available as you progress.").foregroundStyle(.secondary)
            }
            Section("Camera") {
                LabeledContent("Sensitivity",value:String(format:"%.2f×",store.options.sensitivity))
                Slider(value:$store.options.sensitivity,in:0.5...2,step:0.05).accessibilityLabel("Camera sensitivity")
                LabeledContent("Stick dead zone",value:String(format:"%.0f%%",store.options.deadzone*100))
                Slider(value:$store.options.deadzone,in:0.02...0.4,step:0.01).accessibilityLabel("Stick dead zone")
                Toggle("Invert vertical aiming",isOn:$store.options.invertY)
                Toggle("Use right stick for camera aiming",isOn:$store.options.rightStickAim)
                Text("When disabled, the right stick sends the original C-button directions.").font(.caption).foregroundStyle(.secondary)
            }
            Section("Navigation & feedback") {
                Toggle("D-pad navigates menus and the name picker",isOn:$store.options.dpadNavigation)
                Toggle("Controller vibration",isOn:$store.options.rumble)
                Text("Vibration follows game requests; adaptive triggers are not simulated.").font(.caption).foregroundStyle(.secondary)
            }
        }.formStyle(.grouped)
    }
    private var pairing: some View {
        VStack(alignment:.leading,spacing:20) {
            Label("Connect over Bluetooth",systemImage:"antenna.radiowaves.left.and.right").font(.title2.bold())
            Text("1. Unplug the USB cable and turn the controller off.")
            Text("2. Hold Create and PS together until the light bar flashes.")
            Text("3. Open Bluetooth settings and select DualSense Wireless Controller.")
            Button("Open Mac Bluetooth Settings",action:store.bluetooth).buttonStyle(.borderedProminent)
            Divider()
            Text("The game detects connection and reconnection automatically. You can also connect with a USB-C data cable.").foregroundStyle(.secondary)
            Text("The PS button remains a macOS system control. This app does not capture microphone audio.").font(.callout).foregroundStyle(.secondary)
            Link("Apple’s controller pairing guide",destination:URL(string:"https://support.apple.com/en-us/111100")!)
            Spacer()
        }.padding(24)
    }
}
