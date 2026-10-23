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
import android.util.Log;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.RelativeLayout;
import android.widget.TextView;

import java.util.Collections;

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
 * Button setting, and the controls' size and opacity (the settings file's
 * touch_controls, touch_size and touch_opacity).
 */
public class Snap64TouchOverlay {
    static native int nativeState();
    static native void nativeButtons(int bits);

    // The N64 buttons' bits (libultra's CONT_ values; src/input.cpp).
    static final int A = 0x8000, B = 0x4000, Z = 0x2000, START = 0x1000, R = 0x0010;
    static final int C_UP = 0x0008, C_DOWN = 0x0004, C_LEFT = 0x0002, C_RIGHT = 0x0001;

    private static final int BLUE = 0xFF3D5AFE, GREEN = 0xFF2E9E44, RED = 0xFFE03131;
    private static final int YELLOW = 0xFFF5C400, GRAY = 0xFF8A9199;
    private static final String PREFS = "snap64", HINT_SHOWN = "touch_hint_shown";
    private static final String HINT = "Drag to aim. Tap Z to zoom. A takes the photo.";

    private final Context context;
    private final ViewGroup layout;
    private final ButtonView[] buttons;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int state = 0;
    private int held = 0;
    // The overlay on the screen, for the native side's photoTaken.
    private static volatile Snap64TouchOverlay current;
    private int laidOutW = -1, laidOutH = -1, laidOutSize = -1, laidOutCut = -1;
    // The first course's hint: still to show on this install, and whether a
    // ride (a course, not the title's attract demo) ran at the last look.
    private boolean hintDue;
    private boolean wasRide = false;

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            int s = 0;
            try {
                s = nativeState();
            } catch (UnsatisfiedLinkError e) {
                // The library is not up yet.
            }
            if (s != state) {
                state = s;
                refresh();
            }
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
            new ButtonView(context, START, "START", RED, false, 0.62f),
            new ButtonView(context, C_UP, "▲", YELLOW, true, 0.72f),
            new ButtonView(context, C_LEFT, "◀", YELLOW, true, 0.72f),
            new ButtonView(context, C_RIGHT, "▶", YELLOW, true, 0.72f),
            new ButtonView(context, C_DOWN, "▼", YELLOW, true, 0.72f),
            new ButtonView(context, R, "R", GRAY, true, 0.72f),
            new ButtonView(context, Z, "Z", GRAY, true, 0.9f),
            new ButtonView(context, A, "A", BLUE, false, 1.2f),
            new ButtonView(context, B, "B", GREEN, false, 0.9f),
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

    // Visibility, place and look from the latest state and the layout's size.
    private void refresh() {
        final int w = layout.getWidth(), h = layout.getHeight();
        int size = (state >> 8) & 0xFF;
        if (size == 0) {
            size = 100;
        }
        final int cut = cutoutKey();
        if ((w > 0) && (h > 0) && ((w != laidOutW) || (h != laidOutH) || (size != laidOutSize) || (cut != laidOutCut))) {
            laidOutCut = cut;
            laidOutW = w;
            laidOutH = h;
            laidOutSize = size;
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
            if (b.latched && (!shown() || !course() || ((b.bit == Z) && zoomSwitch()))) {
                b.latched = false;
                b.down = false;
                b.invalidate();
                changed = true;
            }
        }
        for (ButtonView b : buttons) {
            final boolean active = shown() && (course() || !b.courseOnly);
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
        if (ride && !wasRide && hintDue && shown() && (h > 0)) {
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
    // enough, over the picture's edges when not.
    private void place(int w, int h, int size) {
        final float base = Math.min(w, h) * 0.085f * (size / 100.0f);
        final float picture = h * 4.0f / 3.0f;
        final float bar = (w - picture) / 2.0f;
        final float column = 2.8f * base;
        // Over the picture, far enough in for C-Left's whole square.
        float left = (bar >= column) ? bar / 2.0f : 1.8f * base;
        float right = w - left;
        // Clear of a camera cutout on either side.
        final WindowInsets insets = layout.getRootWindowInsets();
        final DisplayCutout cutout = (insets != null) ? insets.getDisplayCutout() : null;
        if (cutout != null) {
            left = Math.max(left, cutout.getSafeInsetLeft() + 1.8f * base);
            right = Math.min(right, w - cutout.getSafeInsetRight() - 1.5f * base);
        }
        final float gap = 0.95f * base;   // from the C buttons' middle to each one's center
        // For tools/android_check.py, which presses the buttons where they are.
        Log.i("Snap64Touch", String.format(java.util.Locale.ROOT,
            "columns at %.1f and %.1f on %dx%d, base %.2f", left, right, w, h, base));
        put(START, left, h * 0.13f, base);
        put(C_UP, left, h * 0.42f - gap, base);
        put(C_LEFT, left - gap, h * 0.42f + 0.25f * gap, base);
        put(C_RIGHT, left + gap, h * 0.42f + 0.25f * gap, base);
        put(C_DOWN, left, h * 0.42f + 1.5f * gap, base);
        put(R, right, h * 0.13f, base);
        put(Z, right, h * 0.37f, base);
        put(A, right, h * 0.63f, base);
        put(B, right - 0.2f * base, h * 0.87f, base);
    }

    private void put(int bit, float x, float y, float base) {
        for (ButtonView b : buttons) {
            if (b.bit != bit) {
                continue;
            }
            b.radius = base * b.scale;
            // The view's square reaches a little past the circle, and over
            // 48 dp at the least: a thumb is not a stylus.
            final float floor = 48.0f * context.getResources().getDisplayMetrics().density;
            final int side = Math.round(Math.max(b.radius * 2.3f, floor));
            final RelativeLayout.LayoutParams p = new RelativeLayout.LayoutParams(side, side);
            p.leftMargin = Math.round(x - side / 2.0f);
            p.topMargin = Math.round(y - side / 2.0f);
            b.setLayoutParams(p);
            b.invalidate();
        }
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
        final String label;
        final int color;
        final boolean courseOnly;
        final float scale;   // of the base radius
        float radius = 1.0f;
        int opacity = 60;
        boolean down = false;
        boolean lit = false;      // held by a tap; Z also while the game is zoomed in
        boolean latched = false;  // Z or R held by a tap until the next
        long pressAt = -1;        // when a press that may become a tap began
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

        ButtonView(Context context, int bit, String label, int color, boolean courseOnly, float scale) {
            super(context);
            this.bit = bit;
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

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if ((e.getActionMasked() == MotionEvent.ACTION_DOWN) && !nearest(e)) {
                return false;   // the view below, the nearer button's, takes it
            }
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
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
