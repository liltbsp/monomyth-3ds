package io.github.liltbsp.dualdock;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.TextView;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Milestone 0 test screen (top display).
 *
 * Shows which displays Android reports, every controller button press, and
 * every tap on the bottom-screen dock. The point is to prove three things on
 * the Thor: the dock draws on the bottom screen, taps register, and the
 * controller keeps working after you touch the bottom screen.
 */
public class MainActivity extends Activity implements DockView.Listener, DisplayManager.DisplayListener {

    private static final int MAX_LINES = 14;

    private DisplayManager displayManager;
    private DockPresentation presentation;
    private TextView header;
    private TextView log;
    private final Deque<String> lines = new ArrayDeque<>();
    private long lastStickLog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);

        android.widget.LinearLayout root = new android.widget.LinearLayout(this);
        root.setOrientation(android.widget.LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF1E1230);
        root.setPadding(32, 24, 32, 24);

        header = new TextView(this);
        header.setTextColor(0xFFE8DEF7);
        header.setTextSize(18f);
        header.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(header);

        log = new TextView(this);
        log.setTextColor(Color.WHITE);
        log.setTextSize(14f);
        log.setTypeface(Typeface.MONOSPACE);
        log.setGravity(Gravity.TOP);
        log.setPadding(0, 16, 0, 0);
        root.addView(log);

        setContentView(root);
        root.setFocusable(true);
        root.requestFocus();
        add("Press controller buttons, then tap the bottom screen,");
        add("then press buttons again to check they still work.");
    }

    @Override
    protected void onResume() {
        super.onResume();
        displayManager.registerDisplayListener(this, null);
        showDock();
    }

    @Override
    protected void onPause() {
        super.onPause();
        displayManager.unregisterDisplayListener(this);
        hideDock();
    }

    // ---------------------------------------------------------------- Displays

    private Display findBottomDisplay() {
        Display[] presentationDisplays = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        for (Display d : presentationDisplays) {
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY) {
                return d;
            }
        }
        // Fallback: any display that isn't the main one.
        for (Display d : displayManager.getDisplays()) {
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY) {
                return d;
            }
        }
        return null;
    }

    private String describe(Display d) {
        DisplayMetrics m = new DisplayMetrics();
        d.getRealMetrics(m);
        return "#" + d.getDisplayId() + " \"" + d.getName() + "\" " + m.widthPixels + "x" + m.heightPixels;
    }

    private void showDock() {
        StringBuilder sb = new StringBuilder("DualDock build " + BuildConfig.BUILD_NUMBER + "\nDisplays: ");
        for (Display d : displayManager.getDisplays()) {
            sb.append(describe(d)).append("   ");
        }
        Display bottom = findBottomDisplay();
        if (bottom == null) {
            sb.append("\nNo second screen found. On the Thor, check the bottom screen is enabled.");
            header.setText(sb.toString());
            return;
        }
        sb.append("\nDock on ").append(describe(bottom));
        header.setText(sb.toString());

        if (presentation != null && presentation.getDisplay().getDisplayId() == bottom.getDisplayId()) {
            return;
        }
        hideDock();
        presentation = new DockPresentation(this, bottom, BuildConfig.BUILD_NUMBER);
        presentation.getDock().setListener(this);
        try {
            presentation.show();
        } catch (RuntimeException e) {
            add("Couldn't show dock: " + e.getMessage());
            presentation = null;
        }
    }

    private void hideDock() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        showDock();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (presentation != null && presentation.getDisplay().getDisplayId() == displayId) {
            hideDock();
        }
        showDock();
    }

    @Override
    public void onDisplayChanged(int displayId) {
    }

    // ------------------------------------------------------------------- Input

    @Override
    public void onTileTapped(String name) {
        add("TAP  " + name + "  (bottom screen)");
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            return super.dispatchKeyEvent(event);
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            add("KEY  " + KeyEvent.keyCodeToString(event.getKeyCode()));
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && event.getAction() == MotionEvent.ACTION_MOVE) {
            float x = event.getAxisValue(MotionEvent.AXIS_X);
            float y = event.getAxisValue(MotionEvent.AXIS_Y);
            float hx = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            long now = System.currentTimeMillis();
            boolean moved = Math.abs(x) > 0.5f || Math.abs(y) > 0.5f || hx != 0 || hy != 0;
            if (moved && now - lastStickLog > 250) {
                lastStickLog = now;
                add(String.format("STICK x=%.1f y=%.1f  dpad=%.0f,%.0f", x, y, hx, hy));
            }
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void add(String line) {
        lines.addLast(line);
        while (lines.size() > MAX_LINES) {
            lines.removeFirst();
        }
        StringBuilder sb = new StringBuilder();
        for (String l : lines) {
            sb.append(l).append('\n');
        }
        log.setText(sb.toString());
    }
}
