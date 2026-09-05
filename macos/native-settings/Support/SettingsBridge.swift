import AppKit
import SwiftUI

final class SettingsBridge: NSObject, NSWindowDelegate {
    static let shared=SettingsBridge()
    let store=ControllerStore()
    private var window: NSWindow?
    private var timer: Timer?
    var focus: FocusController?
    func install() {
        guard let menu=NSApp.mainMenu?.items.first?.submenu else {return}
        guard !menu.items.contains(where:{$0.identifier?.rawValue == "snap.settings"}) else {return}
        // SDL installs a disabled Preferences item with the standard shortcut.
        // Reuse it so AppKit's modern Settings presentation has a single owner.
        let item=menu.items.first(where: {$0.keyEquivalent == ","})
            ?? NSMenuItem(title:"Settings…", action:nil, keyEquivalent:",")
        if item.menu == nil { menu.insertItem(item,at:min(1,menu.numberOfItems)) }
        item.title="Settings…"
        item.action=#selector(showSettings)
        item.keyEquivalent=","
        item.keyEquivalentModifierMask = .command
        item.identifier=NSUserInterfaceItemIdentifier("snap.settings")
        item.target=self
        item.isEnabled=true

    }
    @objc func showSettings() {
        if window == nil {
            let host=NSHostingController(rootView: ControllerSettingsView(store:store))
            let panel=NSWindow(contentViewController:host)
            panel.title="Pokémon Snap Settings"
            panel.styleMask=[.titled,.closable,.miniaturizable]
            panel.setContentSize(NSSize(width:660,height:650))
            panel.isReleasedWhenClosed=false
            panel.delegate=self
            panel.center()
            window=panel
        }
        store.refresh()
        focus?(1)
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps:true)
        timer?.invalidate()
        timer=Timer.scheduledTimer(withTimeInterval:0.1,repeats:true) { [weak self] _ in self?.store.refresh() }
    }
    func windowWillClose(_ notification:Notification) {timer?.invalidate();timer=nil;focus?(0)}
    func windowDidBecomeKey(_ notification:Notification) {focus?(1)}
    func windowDidResignKey(_ notification:Notification) {focus?(0)}
}
@_cdecl("snap_settings_install")
public func installSettings(_ read: @convention(c) () -> UnsafePointer<CChar>?,
                            _ save: @convention(c) (UnsafePointer<CChar>?) -> Int32,
                            _ test: @convention(c) () -> Int32,
                            _ focus: @convention(c) (Int32) -> Void) {
    let bridge=SettingsBridge.shared
    bridge.store.read=read;bridge.store.save=save;bridge.store.test=test;bridge.focus=focus
    bridge.install()
}
@_cdecl("snap_settings_show")
public func showSettings() {
    DispatchQueue.main.async {SettingsBridge.shared.showSettings()}
}
