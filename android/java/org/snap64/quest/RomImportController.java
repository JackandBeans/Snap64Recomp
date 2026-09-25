package org.snap64.quest;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import java.io.*;
import java.util.Locale;

/** File access only. Instructions, confirmation, and progress are rendered in OpenXR. */
final class RomImportController {
    static final int PICK_ROM = 0x534E;
    // Native splash reads these volatile fields through QuestActivity methods.
    volatile int state = 0, progress = 0; // checking, choose, picker, copying, ready, error
    volatile String message = "Checking game file...";
    private final Activity activity;
    private final File destination;
    private Thread worker;
    RomImportController(Activity activity, File directory) {
        this.activity = activity; destination = new File(directory, RomImporter.ROM_NAME);
        worker = new Thread(() -> {
            if (!destination.exists()) { message = ""; state = 1; return; }
            try { RomImporter.validate(destination); state = 4; }
            catch (IOException e) { message = "Saved ROM could not be verified. Import it again."; state = 5; }
        }, "Snap-ROM-check"); worker.start();
    }
    void choose() {
        activity.runOnUiThread(() -> {
            if (state != 1 && state != 5) return;
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.addCategory(Intent.CATEGORY_OPENABLE); picker.setType("*/*");
            picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            state = 2; message = "Choose your ROM in the system file picker.";
            try { activity.startActivityForResult(picker, PICK_ROM); }
            catch (ActivityNotFoundException e) { message = "System file picker unavailable. Select OK to retry."; state = 5; }
        });
    }
    void result(int result, Intent data) {
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) {
            message = "No file selected. Select OK when you are ready."; state = 1; return;
        }
        final Uri uri = data.getData(); state = 3; progress = 0; message = "Importing ROM...";
        worker = new Thread(() -> {
            try {
                long size = -1; String name = null;
                try (Cursor cursor = activity.getContentResolver().query(uri,
                        new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) {
                        int n = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME), s = cursor.getColumnIndex(OpenableColumns.SIZE);
                        if (n >= 0 && !cursor.isNull(n)) name = cursor.getString(n);
                        if (s >= 0 && !cursor.isNull(s)) size = cursor.getLong(s);
                    }
                }
                if (name != null && !name.toLowerCase(Locale.ROOT).endsWith(".z64"))
                    throw new IOException("Choose a .z64 ROM, not an archive or another format.");
                RomImporter.importRom(activity.getContentResolver().openInputStream(uri), size, destination,
                    value -> { progress = value; message = value >= 96 ? "Verifying ROM..." : "Importing ROM..."; });
                progress = 100; message = "Import complete. Starting game..."; state = 4;
            } catch (IOException | RuntimeException e) {
                android.util.Log.w("SnapRomImport", "ROM import failed", e);
                message = e instanceof IOException ? e.getMessage() : "File could not be read. Please choose it again.";
                state = 5;
            }
        }, "Snap-ROM-import"); worker.start();
    }
    void stop() { if (worker != null) worker.interrupt(); }
}
