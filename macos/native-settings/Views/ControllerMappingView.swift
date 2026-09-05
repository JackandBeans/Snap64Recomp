import SwiftUI
struct ControllerMappingView: View {
    private let rows:[(String,String)] = [
        ("Left stick", "Look / aim · navigate menus"),
        ("Right stick", "Look / aim (or C-buttons in Controls)"),
        ("D-pad", "Navigate menus / name picker + N64 D-pad"),
        ("L2", "Hold to aim the camera · N64 Z"),
        ("R2 / Cross ✕", "Photo while aiming / food / confirm · N64 A"),
        ("Circle ○ / Square □", "Pester Ball / cancel · N64 B"),
        ("Triangle △", "Poké Flute · C-down"),
        ("L1 / R1", "Quick turn left / right · C-left / C-right"),
        ("L3 (left stick click)", "Dash Engine · N64 R"),
        ("R3 (right stick click)", "Turn around · C-up"),
        ("Options", "Pause / Start"),
        ("Create", "Export the displayed photo"),
        ("Touchpad click", "Open these settings"),
        ("Mute", "Toggle game audio (when reported by controller)"),
        ("PS", "Managed by macOS")
    ]
    var body: some View {
        ScrollView {
            VStack(alignment:.leading,spacing:0) {
                ForEach(rows,id:\.0) {row in
                    HStack(alignment:.top) {
                        Text(row.0).fontWeight(.medium).frame(width:190,alignment:.leading)
                        Text(row.1).foregroundStyle(.secondary).frame(maxWidth:.infinity,alignment:.leading)
                    }.padding(.vertical,9)
                    Divider()
                }
                Text("Camera and item actions follow the original game's context and unlocks. The controller microphone is not used.")
                    .font(.caption).foregroundStyle(.secondary).padding(.top,14)
            }.padding(20)
        }
    }
}
