import SwiftUI
struct ControllerTestView: View {
    var store:ControllerStore
    private let buttons:[(String,Int)] = [("✕",0),("○",1),("□",2),("△",3),("Create",4),("PS",5),("Options",6),("L3",7),("R3",8),("L1",9),("R1",10),("↑",11),("↓",12),("←",13),("→",14),("Mute",15),("Touchpad",20)]
    var body:some View {
        VStack(spacing:20) {
            Text("Live controller input").font(.headline)
            Text("Inputs are held out of the game while this window has focus.").font(.callout).foregroundStyle(.secondary)
            HStack(spacing:42) {
                stick("Left stick",x:axis(0),y:axis(1))
                stick("Right stick",x:axis(2),y:axis(3))
                VStack(alignment:.leading,spacing:12) {
                    Text("L2");ProgressView(value:max(0,axis(4)))
                    Text("R2");ProgressView(value:max(0,axis(5)))
                }.frame(width:110)
            }
            LazyVGrid(columns:[GridItem(.adaptive(minimum:65))],spacing:8) {
                ForEach(buttons,id:\.1) {label,index in
                    Text(label).font(.callout.monospaced()).frame(maxWidth:.infinity).padding(.vertical,8)
                        .background(pressed(index) ? Color.accentColor.opacity(0.3) : Color.secondary.opacity(0.1),in:RoundedRectangle(cornerRadius:7))
                        .accessibilityLabel("\(label): \(pressed(index) ? "pressed":"released")")
                }
            }
            Button("Test Vibration",action:store.testRumble).disabled(!store.status.canRumble || !store.options.rumble)
            if !store.status.connected {Text("Connect your DualSense to test buttons and sticks.").foregroundStyle(.secondary)}
            Spacer(minLength:0)
        }.padding(24)
    }
    private func axis(_ i:Int)->Double {store.status.axes.indices.contains(i) ? store.status.axes[i]:0}
    private func pressed(_ i:Int)->Bool {store.status.buttons.indices.contains(i) && store.status.buttons[i]}
    private func stick(_ title:String,x:Double,y:Double)->some View {
        VStack(spacing:8) {
            ZStack {
                Circle().stroke(.secondary.opacity(0.4),lineWidth:1)
                Rectangle().fill(.secondary.opacity(0.2)).frame(height:1)
                Rectangle().fill(.secondary.opacity(0.2)).frame(width:1)
                Circle().fill(Color.accentColor).frame(width:18,height:18).offset(x:x*40,y:y*40)
            }.frame(width:100,height:100)
            Text(title).font(.caption)
        }
    }
}
