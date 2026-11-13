package io.github.jackandbeans.snap64recomp;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;

import java.util.Collections;
import java.util.Locale;

/**
 * The game's buttons drawn over the picture, for a phone with no pad (the
 * native side is src/touch_controls.cpp).
 *
 * In a course: A, B, Z, R, the four C buttons and Start; outside one: A, B
 * and Start. On a wide screen they stand in the black bars beside the 4:3
 * picture, the left bar holding Start and the C buttons, the right the
 * shoulder, Z, A and B; on a narrower one they sit over its edges. Colored
 * as the N64 controller's own: A blue, B green, Start red, the C buttons
 * yellow, Z and R gray.
 *
 * Each button is a small view of its own over SDL's surface, so a touch
 * anywhere else reaches the game whatever the other fingers hold: a drag
 * aims in a course while a thumb presses A, and a tap on a menu is the
 * pointer's click. A press is held until that finger lifts; the buttons
 * held go to the native side whole at every change. Z and R are the
 * exceptions: a tap (under 300 ms) holds the button until the next tap, so
 * the thumb that zoomed or started the dash is free for A, and a longer
 * press is a hold that ends at the lift, as on the N64. The game zooms only
 * while Z is down in its own Hold mode (Options, Controls, Z Button) and
 * dashes only while R is down; in its Switch mode the game toggles the zoom
 * at each press itself, and Z is a plain button. A held button is drawn lit
 * (a second ring), and Z also while the game's view is zoomed in. A press
 * and a release are felt when the phone's Touch feedback is on, and the
 * moment the game takes a photo firmer (photoTaken, from the native side).
 * Each button takes touches over at least 48 dp, however small it is drawn;
 * where two such squares overlap, the button whose circle is nearer takes
 * the touch.
 *
 * The first course on an install with the controls shown puts one line
 * over the picture's lower middle for five seconds: how to aim, zoom and
 * take a photo. The launching intent's SNAP_TOUCH_HINT ("show" or "off")
 * overrides that, for tools/android_check.py.
 *
 * The native side says every tenth of a second whether the controls show
 * (automatic: until a pad or a keyboard is used, and again at the next
 * touch), whether a course runs, whether the game is zoomed in, its Z
 * Button setting, and the controls' settings, which the CONTROLS page has
 * rows for on Android: their size and opacity, Left-Handed, which swaps the
 * two columns, and Idle Fade, which dims them after four seconds without a
 * press until the next (the settings file's touch_controls, touch_size,
 * touch_opacity, touch_mirror and touch_fade).
 *
 * The page's Move Buttons opens a mode over the page for placing them: every
 * button shows, a finger drags one where the thumb wants it, Reset puts them
 * all back, Done keeps the places (touch_layout: each moved button's offset
 * from its own place, in thousandths of the screen's height, as the
 * right-handed layout has it, so Left-Handed mirrors the moved places too).
 * Nothing reaches the game while it is open.
 */
public class Snap64TouchOverlay {
    static native int nativeState();
    static native void nativeButtons(int bits);
    static native String nativeLayout();
    static native void nativeSetLayout(String layout);

    // The N64 buttons' bits (libultra's CONT_ values; src/input.cpp).
    static final int A = 0x8000, B = 0x4000, Z = 0x2000, START = 0x1000, R = 0x0010;
    static final int C_UP = 0x0008, C_DOWN = 0x0004, C_LEFT = 0x0002, C_RIGHT = 0x0001;

    private static final int BLUE = 0xFF3D5AFE, GREEN = 0xFF2E9E44, RED = 0xFFE03131;
    private static final int YELLOW = 0xFFF5C400, GRAY = 0xFF8A9199;
    private static final String PREFS = "snap64", HINT_SHOWN = "touch_hint_shown";
    private static final String HINT = "Drag to aim. Tap Z to zoom. A takes the photo.";
    // Idle Fade: after this long without a press the buttons are drawn at
    // this share of their opacity, until the next press.
    private static final long FADE_AFTER_MS = 4000;
    private static final float FADED = 0.3f;

    private final Context context;
    private final ViewGroup layout;
    private final ButtonView[] buttons;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int state = 0;
    private int held = 0;
    // The overlay on the screen, for the native side's photoTaken.
    private static volatile Snap64TouchOverlay current;
    private int laidOutW = -1, laidOutH = -1, laidOutSize = -1, laidOutCut = -1, laidOutMirror = -1;
    // The first course's hint: still to show on this install, and whether a
    // ride (a course, not the title's attract demo) ran at the last look.
    private boolean hintDue;
    private boolean wasRide = false;
    // Where Move Buttons put the buttons, read once the library is up.
    private boolean layoutRead = false;
    // Idle Fade: when a button was last pressed (uptime), and whether the
    // buttons are drawn faded now.
    private long lastPress = 0;
    private boolean faded = false;
    private boolean fadeWas = false;
    private boolean shownWas = false;
    // Move Buttons: the page's count of presses last seen, and the mode's
    // views while it is open.
    private int movesSeen = -1;
    private boolean editing = false;
    private View blocker;
    private View panel;

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            int s = 0;
            boolean placesRead = false;
            try {
                s = nativeState();
                if (!layoutRead) {
                    readLayout(nativeLayout());
                    layoutRead = true;
                    placesRead = true;
                    laidOutW = -1;
                }
            } catch (UnsatisfiedLinkError e) {
                // The library is not up yet.
            }
            if ((s != state) || placesRead) {
                state = s;
                refresh();
            }
            tickFade();
            handler.postDelayed(this, 100);
        }
    };

    public Snap64TouchOverlay(Context context, ViewGroup layout) {
        this.context = context;
        this.layout = layout;
        final String hintSay = (context instanceof Activity) && (((Activity) context).getIntent() != null)
            ? ((Activity) context).getIntent().getStringExtra("SNAP_TOUCH_HINT") : null;
        hintDue = "show".equals(hintSay) ||
            (!"off".equals(hintSay) && !prefs().getBoolean(HINT_SHOWN, false));
        buttons = new ButtonView[] {
            new ButtonView(context, START, "START", "START", RED, false, 0.62f),
            new ButtonView(context, C_UP, "CU", "▲", YELLOW, true, 0.72f),
            new ButtonView(context, C_LEFT, "CL", "◀", YELLOW, true, 0.72f),
            new ButtonView(context, C_RIGHT, "CR", "▶", YELLOW, true, 0.72f),
            new ButtonView(context, C_DOWN, "CD", "▼", YELLOW, true, 0.72f),
            new ButtonView(context, R, "R", "R", GRAY, true, 0.72f),
            new ButtonView(context, Z, "Z", "Z", GRAY, true, 0.9f),
            new ButtonView(context, A, "A", "A", BLUE, false, 1.2f),
            new ButtonView(context, B, "B", "B", GREEN, false, 0.9f),
        };
        for (ButtonView b : buttons) {
            b.setVisibility(View.GONE);
            layout.addView(b, new RelativeLayout.LayoutParams(1, 1));
        }
        layout.addOnLayoutChangeListener((v, l, t, r, bottom, ol, ot, or, ob) -> refresh());
        handler.post(poll);
        current = this;
    }

    public void stop() {
        handler.removeCallbacks(poll);
        if (editing) {
            stopEditing(false);
        }
        release();
        if (current == this) {
            current = null;
        }
    }

    // Lets go of every button (the app goes to the background).
    public void release() {
        for (ButtonView b : buttons) {
            b.latched = false;
            b.pressAt = -1;
            if (b.down) {
                b.down = false;
                b.invalidate();
            }
        }
        publish();
    }

    // The game has just taken a photo (src/touch_controls.cpp, on SDL's
    // thread): the shutter is felt firmer than a press, from A while it is
    // on the screen.
    static void photoTaken() {
        final Snap64TouchOverlay o = current;
        if (o != null) {
            o.handler.post(o::shutter);
        }
    }

    private void shutter() {
        for (ButtonView b : buttons) {
            if ((b.bit == A) && (b.getVisibility() == View.VISIBLE)) {
                b.performHapticFeedback((Build.VERSION.SDK_INT >= 30)
                    ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.LONG_PRESS);
            }
        }
    }

    private SharedPreferences prefs() {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private boolean shown() {
        return (state & 1) != 0;
    }

    private boolean course() {
        return (state & 2) != 0;
    }

    private boolean zoomed() {
        return (state & 4) != 0;
    }

    // The course is the title's attract demo, not a ride.
    private boolean demo() {
        return (state & 16) != 0;
    }

    // The game's Z Button is Switch: it toggles the zoom at each press.
    private boolean zoomSwitch() {
        return (state & 8) != 0;
    }

    // Left-Handed: Start and the C buttons on the right, A and the rest on
    // the left.
    private boolean mirror() {
        return (state & 32) != 0;
    }

    // Idle Fade.
    private boolean fade() {
        return (state & 64) != 0;
    }

    // Visibility, place and look from the latest state and the layout's size.
    private void refresh() {
        final int w = layout.getWidth(), h = layout.getHeight();
        int size = (state >> 8) & 0xFF;
        if (size == 0) {
            size = 100;
        }
        // The CONTROLS page's Move Buttons, counted: a new press opens the
        // mode (not the count found at the start).
        final int moves = (state >>> 24) & 0xF;
        if (movesSeen < 0) {
            movesSeen = moves;
        } else if (moves != movesSeen) {
            movesSeen = moves;
            startEditing();
        }
        final int cut = cutoutKey();
        final int mir = mirror() ? 1 : 0;
        if ((w > 0) && (h > 0) && ((w != laidOutW) || (h != laidOutH) || (size != laidOutSize) || (cut != laidOutCut) ||
                                   (mir != laidOutMirror))) {
            laidOutCut = cut;
            laidOutW = w;
            laidOutH = h;
            laidOutSize = size;
            laidOutMirror = mir;
            place(w, h, size);
        }
        int opacity = (state >> 16) & 0xFF;
        if (opacity == 0) {
            opacity = 60;
        }
        boolean changed = false;
        // A latched Z or R lets go when no course runs and when the controls
        // hide (a pad or a keyboard is in use); Z also when the game's Z
        // Button is Switch, where a Z held down would swallow every other
        // press.
        for (ButtonView b : buttons) {
            if (b.latched && (editing || !shown() || !course() || ((b.bit == Z) && zoomSwitch()))) {
                b.latched = false;
                b.down = false;
                b.invalidate();
                changed = true;
            }
        }
        // Idle Fade counts from when the buttons appear or the fade is
        // turned on, as well as from each press.
        if ((shown() && !shownWas) || (fade() && !fadeWas)) {
            lastPress = SystemClock.uptimeMillis();
        }
        shownWas = shown();
        fadeWas = fade();
        for (ButtonView b : buttons) {
            // Move Buttons shows every button, a course's set.
            final boolean active = editing || (shown() && (course() || !b.courseOnly));
            final int vis = active ? View.VISIBLE : View.GONE;
            if (b.getVisibility() != vis) {
                b.setVisibility(vis);
                if (!active && b.down) {
                    b.down = false;
                    changed = true;
                }
            }
            if (b.opacity != opacity) {
                b.opacity = opacity;
                b.invalidate();
            }
            b.updateLit();
        }
        if (changed) {
            publish();
        }
        final boolean ride = course() && !demo();
        if (ride && !wasRide && hintDue && shown() && !editing && (h > 0)) {
            showHint(h);
        }
        wasRide = ride;
    }

    // One line over the picture's lower middle for five seconds, then gone.
    // It takes no touches: a drag through it aims.
    private void showHint(int h) {
        hintDue = false;
        prefs().edit().putBoolean(HINT_SHOWN, true).apply();
        final TextView t = new TextView(context);
        t.setText(HINT);
        t.setTextColor(Color.WHITE);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        t.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        final float dp = context.getResources().getDisplayMetrics().density;
        t.setPadding(Math.round(14 * dp), Math.round(8 * dp), Math.round(14 * dp), Math.round(8 * dp));
        final GradientDrawable box = new GradientDrawable();
        box.setColor(0xA0000000);
        box.setCornerRadius(10 * dp);
        t.setBackground(box);
        t.setClickable(false);
        t.setFocusable(false);
        final RelativeLayout.LayoutParams p = new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
        p.addRule(RelativeLayout.CENTER_HORIZONTAL);
        p.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        p.bottomMargin = Math.round(h * 0.12f);
        layout.addView(t, p);
        Log.i("Snap64Touch", "the first course's hint is shown");
        handler.postDelayed(() -> t.animate().alpha(0.0f).setDuration(400).withEndAction(() -> {
            layout.removeView(t);
            Log.i("Snap64Touch", "the first course's hint is gone");
        }), 5000);
    }

    // The cutout's safe insets, left and right, as one number.
    private int cutoutKey() {
        final WindowInsets insets = layout.getRootWindowInsets();
        final DisplayCutout cutout = (insets != null) ? insets.getDisplayCutout() : null;
        return (cutout != null) ? (cutout.getSafeInsetLeft() * 65536 + cutout.getSafeInsetRight()) : 0;
    }

    // Two columns, in the bars beside the 4:3 picture when they are wide
    // enough, over the picture's edges when not: Start and the C buttons in
    // the left one, R, Z, A and B in the right, or the other way round when
    // Left-Handed is on (the C buttons keep their own left and right). Each
    // button then moves by its Move Buttons offset, mirrored with the rest.
    private void place(int w, int h, int size) {
        final float base = Math.min(w, h) * 0.085f * (size / 100.0f);
        final float picture = h * 4.0f / 3.0f;
        final float bar = (w - picture) / 2.0f;
        final float column = 2.8f * base;
        final boolean m = mirror();
        // Over the picture, far enough in for C-Left's whole square.
        float left = (bar >= column) ? bar / 2.0f : 1.8f * base;
        float right = w - left;
        // Clear of a camera cutout on either side: the C buttons' side keeps
        // 1.8 bases from it, A's side 1.5.
        final WindowInsets insets = layout.getRootWindowInsets();
        final DisplayCutout cutout = (insets != null) ? insets.getDisplayCutout() : null;
        if (cutout != null) {
            left = Math.max(left, cutout.getSafeInsetLeft() + (m ? 1.5f : 1.8f) * base);
            right = Math.min(right, w - cutout.getSafeInsetRight() - (m ? 1.8f : 1.5f) * base);
        }
        final float c = m ? right : left;          // Start and the C buttons
        final float s = m ? left : right;          // R, Z, A and B
        final float in = m ? 1.0f : -1.0f;         // from A's column toward the middle
        final float gap = 0.95f * base;   // from the C buttons' middle to each one's center
        // For tools/android_check.py, which presses the buttons where they are.
        if (!editing) {
            Log.i("Snap64Touch", String.format(Locale.ROOT,
                "columns at %.1f and %.1f on %dx%d, base %.2f", left, right, w, h, base));
        }
        put(START, c, h * 0.13f, base, w, h);
        put(C_UP, c, h * 0.42f - gap, base, w, h);
        put(C_LEFT, c - gap, h * 0.42f + 0.25f * gap, base, w, h);
        put(C_RIGHT, c + gap, h * 0.42f + 0.25f * gap, base, w, h);
        put(C_DOWN, c, h * 0.42f + 1.5f * gap, base, w, h);
        put(R, s, h * 0.13f, base, w, h);
        put(Z, s, h * 0.37f, base, w, h);
        put(A, s, h * 0.63f, base, w, h);
        put(B, s + in * 0.2f * base, h * 0.87f, base, w, h);
        if (!editing) {
            final StringBuilder at = new StringBuilder();
            for (ButtonView b : buttons) {
                at.append(String.format(Locale.ROOT, " %s %.0f,%.0f", b.key, b.cx, b.cy));
            }
            Log.i("Snap64Touch", "buttons at" + at + (m ? " (left-handed)" : ""));
        }
    }

    // A button's center at (x, y), moved by its offset (thousandths of the
    // height, mirrored when Left-Handed is on), its whole circle kept on the
    // screen; an offset that would take it off is cut back to the edge.
    private void put(int bit, float x, float y, float base, int w, int h) {
        for (ButtonView b : buttons) {
            if (b.bit != bit) {
                continue;
            }
            b.radius = base * b.scale;
            final float sign = mirror() ? -1.0f : 1.0f;
            float px = x + sign * b.dx * h / 1000.0f;
            float py = y + b.dy * h / 1000.0f;
            final float cx = Math.max(b.radius, Math.min(w - b.radius, px));
            final float cy = Math.max(b.radius, Math.min(h - b.radius, py));
            if ((cx != px) || (cy != py)) {
                b.dx = sign * (cx - x) * 1000.0f / h;
                b.dy = (cy - y) * 1000.0f / h;
            }
            b.cx = cx;
            b.cy = cy;
            // The view's square reaches a little past the circle, and over
            // 48 dp at the least: a thumb is not a stylus.
            final float floor = 48.0f * context.getResources().getDisplayMetrics().density;
            final int side = Math.round(Math.max(b.radius * 2.3f, floor));
            final RelativeLayout.LayoutParams p = new RelativeLayout.LayoutParams(side, side);
            p.leftMargin = Math.round(cx - side / 2.0f);
            p.topMargin = Math.round(cy - side / 2.0f);
            b.setLayoutParams(p);
            b.invalidate();
        }
    }

    // Lays the buttons out again now (a drag, Reset).
    private void placeAgain() {
        laidOutW = -1;
        refresh();
    }

    // touch_layout: "<key>:<dx>,<dy>" joined by ';'. What cannot be read is
    // skipped; a button not named stays where it came.
    private void readLayout(String text) {
        for (ButtonView b : buttons) {
            b.dx = 0.0f;
            b.dy = 0.0f;
        }
        if (text == null) {
            return;
        }
        for (String entry : text.split(";")) {
            final int colon = entry.indexOf(':');
            final int comma = entry.indexOf(',');
            if ((colon <= 0) || (comma <= colon)) {
                continue;
            }
            final String key = entry.substring(0, colon).trim();
            try {
                final float dx = Integer.parseInt(entry.substring(colon + 1, comma).trim());
                final float dy = Integer.parseInt(entry.substring(comma + 1).trim());
                for (ButtonView b : buttons) {
                    if (b.key.equals(key) && (Math.abs(dx) <= 2000) && (Math.abs(dy) <= 1000)) {
                        b.dx = dx;
                        b.dy = dy;
                    }
                }
            } catch (NumberFormatException e) {
                // skipped
            }
        }
    }

    private String layoutText() {
        final StringBuilder out = new StringBuilder();
        for (ButtonView b : buttons) {
            final int dx = Math.round(b.dx), dy = Math.round(b.dy);
            if ((dx != 0) || (dy != 0)) {
                if (out.length() > 0) {
                    out.append(';');
                }
                out.append(b.key).append(':').append(dx).append(',').append(dy);
            }
        }
        return out.toString();
    }

    // Idle Fade: the buttons dim after FADE_AFTER_MS without a press, and
    // come back at the next; never while Move Buttons is open.
    private void tickFade() {
        final boolean want = fade() && !editing && (SystemClock.uptimeMillis() - lastPress > FADE_AFTER_MS);
        if (want == faded) {
            return;
        }
        faded = want;
        for (ButtonView b : buttons) {
            b.animate().alpha(faded ? FADED : 1.0f).setDuration(faded ? 400 : 120);
        }
        Log.i("Snap64Touch", faded ? "idle: the buttons fade" : "the buttons are back");
    }

    // Move Buttons: every button over a dimmed screen that takes every other
    // touch, a line saying what to do, and Reset and Done.
    private void startEditing() {
        if (editing) {
            return;
        }
        editing = true;
        for (ButtonView b : buttons) {
            b.latched = false;
            b.down = false;
            b.pressAt = -1;
        }
        publish();
        final float dp = context.getResources().getDisplayMetrics().density;
        blocker = new View(context);
        blocker.setBackgroundColor(0x99000000);
        blocker.setOnTouchListener((v, e) -> true);
        layout.addView(blocker, layout.indexOfChild(buttons[0]), new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));

        final LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER_HORIZONTAL);
        final int pad = Math.round(14 * dp);
        box.setPadding(pad, pad, pad, pad);
        final GradientDrawable back = new GradientDrawable();
        back.setColor(0xE0202020);
        back.setCornerRadius(12 * dp);
        box.setBackground(back);
        final TextView say = new TextView(context);
        say.setText("Drag a button to move it.");
        say.setTextColor(Color.WHITE);
        say.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        box.addView(say);
        final LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, Math.round(12 * dp), 0, 0);
        row.addView(choice("Reset", dp, v -> {
            for (ButtonView b : buttons) {
                b.dx = 0.0f;
                b.dy = 0.0f;
            }
            Log.i("Snap64Touch", "Move Buttons: Reset, every button where it came");
            placeAgain();
        }));
        row.addView(choice("Done", dp, v -> stopEditing(true)));
        box.addView(row);
        final RelativeLayout.LayoutParams p = new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
        p.addRule(RelativeLayout.CENTER_IN_PARENT);
        layout.addView(box, p);
        panel = box;
        Log.i("Snap64Touch", "Move Buttons: open");
        placeAgain();
    }

    private TextView choice(String text, float dp, View.OnClickListener click) {
        final TextView t = new TextView(context);
        t.setText(text);
        t.setTextColor(Color.WHITE);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        t.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        final int padH = Math.round(22 * dp), padV = Math.round(10 * dp);
        t.setPadding(padH, padV, padH, padV);
        // A thumb's target: 48 dp at the least, each way.
        t.setMinHeight(Math.round(48 * dp));
        t.setMinWidth(Math.round(96 * dp));
        t.setGravity(Gravity.CENTER);
        final GradientDrawable edge = new GradientDrawable();
        edge.setColor(0xFF3A3A3A);
        edge.setStroke(Math.round(2 * dp), Color.WHITE);
        edge.setCornerRadius(8 * dp);
        t.setBackground(edge);
        t.setOnClickListener(click);
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = Math.round(8 * dp);
        lp.rightMargin = Math.round(8 * dp);
        t.setLayoutParams(lp);
        return t;
    }

    // Done keeps the places (keep); otherwise the ones kept before return.
    private void stopEditing(boolean keep) {
        if (!editing) {
            return;
        }
        editing = false;
        if (keep) {
            final String text = layoutText();
            try {
                nativeSetLayout(text);
            } catch (UnsatisfiedLinkError e) {
                // The library is not up; nothing to keep it in.
            }
            Log.i("Snap64Touch", "Move Buttons: Done, " + (text.isEmpty() ? "every button where it came" : text));
        } else {
            try {
                readLayout(nativeLayout());
            } catch (UnsatisfiedLinkError e) {
                // as they are
            }
        }
        if (blocker != null) {
            layout.removeView(blocker);
            blocker = null;
        }
        if (panel != null) {
            layout.removeView(panel);
            panel = null;
        }
        for (ButtonView b : buttons) {
            b.down = false;
            b.invalidate();
        }
        lastPress = SystemClock.uptimeMillis();
        placeAgain();
    }

    private void publish() {
        int bits = 0;
        for (ButtonView b : buttons) {
            if (b.down) {
                bits |= b.bit;
            }
        }
        if (bits != held) {
            held = bits;
            try {
                nativeButtons(bits);
            } catch (UnsatisfiedLinkError e) {
                // The library is not up yet.
            }
        }
    }

    // How far a point is from a button's circle (negative inside it).
    private static float edge(ButtonView b, float x, float y) {
        final float cx = b.getLeft() + b.getWidth() / 2.0f, cy = b.getTop() + b.getHeight() / 2.0f;
        return (float) Math.hypot(x - cx, y - cy) - b.radius;
    }

    private final class ButtonView extends View {
        final int bit;
        final String key;    // its name in touch_layout
        final String label;
        final int color;
        final boolean courseOnly;
        final float scale;   // of the base radius
        float radius = 1.0f;
        float dx = 0.0f, dy = 0.0f;   // Move Buttons' offset, thousandths of the height, right-handed
        float cx = 0.0f, cy = 0.0f;   // the center it was put at
        int opacity = 60;
        boolean down = false;
        boolean lit = false;      // held by a tap; Z also while the game is zoomed in
        boolean latched = false;  // Z or R held by a tap until the next
        long pressAt = -1;        // when a press that may become a tap began
        // A drag in Move Buttons: where the finger and the offset were.
        private float grabX, grabY, grabDx, grabDy;
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

        ButtonView(Context context, int bit, String key, String label, int color, boolean courseOnly, float scale) {
            super(context);
            this.bit = bit;
            this.key = key;
            this.label = label;
            this.color = color;
            this.courseOnly = courseOnly;
            this.scale = scale;
            ring.setStyle(Paint.Style.STROKE);
            text.setTextAlign(Paint.Align.CENTER);
            text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            super.onLayout(changed, l, t, r, b);
            // Android's back gesture starts at the screen's side edges: a
            // thumb on a button there is the button's, not the gesture's.
            setSystemGestureExclusionRects(Collections.singletonList(new Rect(0, 0, r - l, b - t)));
        }

        // Z in the game's Hold mode, and R: a tap holds, a longer press is a hold.
        boolean tapHolds() {
            return ((bit == Snap64TouchOverlay.Z) && !zoomSwitch()) || (bit == Snap64TouchOverlay.R);
        }

        void updateLit() {
            final boolean now = latched || ((bit == Snap64TouchOverlay.Z) && zoomed());
            if (now != lit) {
                lit = now;
                invalidate();
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            final float o = opacity / 100.0f;
            final float cx = getWidth() / 2.0f, cy = getHeight() / 2.0f;
            final boolean on = down || lit;
            fill.setColor(color);
            fill.setAlpha(Math.round(255 * o * (on ? 0.85f : 0.45f)));
            canvas.drawCircle(cx, cy, radius, fill);
            ring.setColor(Color.WHITE);
            ring.setAlpha(Math.round(255 * o * (on ? 1.0f : 0.8f)));
            final float stroke = Math.max(2.0f, radius * 0.07f);
            ring.setStrokeWidth(stroke);
            canvas.drawCircle(cx, cy, radius, ring);
            if (lit) {
                // A second ring outside the first: Z is on. It stays inside
                // the view's square, which reaches 1.15 radii.
                ring.setStrokeWidth(stroke * 0.6f);
                canvas.drawCircle(cx, cy, radius * 1.1f, ring);
            }
            text.setColor(Color.WHITE);
            text.setAlpha(Math.round(255 * Math.min(1.0f, o + 0.2f)));
            text.setTextSize(label.length() > 2 ? radius * 0.42f : radius * 0.8f);
            final Paint.FontMetrics m = text.getFontMetrics();
            canvas.drawText(label, cx, cy - (m.ascent + m.descent) / 2.0f, text);
        }

        // Whether this button's circle is the nearest of the buttons shown to
        // a touch in its square: a square of 48 dp can overlap a neighbor's.
        private boolean nearest(MotionEvent e) {
            final float x = getLeft() + e.getX(), y = getTop() + e.getY();
            final float mine = edge(this, x, y);
            if (mine <= 0.0f) {
                return true;
            }
            for (ButtonView b : buttons) {
                if ((b != this) && (b.getVisibility() == View.VISIBLE) && (edge(b, x, y) < mine)) {
                    return false;
                }
            }
            return true;
        }

        // Move Buttons: the finger drags the button; nothing is pressed.
        private boolean drag(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    grabX = e.getRawX();
                    grabY = e.getRawY();
                    grabDx = dx;
                    grabDy = dy;
                    down = true;
                    invalidate();
                    return true;
                case MotionEvent.ACTION_MOVE: {
                    final float h = Math.max(1, layout.getHeight());
                    final float mx = (e.getRawX() - grabX) * 1000.0f / h;
                    dx = grabDx + (mirror() ? -mx : mx);
                    dy = grabDy + (e.getRawY() - grabY) * 1000.0f / h;
                    placeAgain();
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    down = false;
                    invalidate();
                    Log.i("Snap64Touch", String.format(Locale.ROOT, "Move Buttons: %s now at %.0f,%.0f", key, cx, cy));
                    return true;
                default:
                    return true;
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if ((e.getActionMasked() == MotionEvent.ACTION_DOWN) && !nearest(e)) {
                return false;   // the view below, the nearer button's, takes it
            }
            if (editing) {
                return drag(e);
            }
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    // Idle Fade: a press brings the buttons back at once.
                    lastPress = SystemClock.uptimeMillis();
                    tickFade();
                    if (latched) {
                        // The tap that lets go of a held Z or R.
                        latched = false;
                        down = false;
                        pressAt = -1;
                    } else {
                        down = true;
                        pressAt = tapHolds() ? e.getEventTime() : -1;
                    }
                    // Felt as the phone's own keys are, when its Touch
                    // feedback is on.
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                    break;
                case MotionEvent.ACTION_UP:
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY_RELEASE);
                    // Under 300 ms a tap: the button stays held. Longer, a
                    // hold: it ends here.
                    latched = (pressAt >= 0) && (e.getEventTime() - pressAt < 300);
                    down = latched;
                    pressAt = -1;
                    break;
                case MotionEvent.ACTION_CANCEL:
                    pressAt = -1;
                    down = latched;
                    break;
                default:
                    return true;
            }
            updateLit();
            invalidate();
            publish();
            return true;
        }
    }
}
