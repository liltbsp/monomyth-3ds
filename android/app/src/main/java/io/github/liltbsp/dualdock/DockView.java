package io.github.liltbsp.dualdock;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

/**
 * The bottom-screen dock, drawn on a 320x240 design grid and scaled to fit
 * whatever display it lands on. Milestone 0 only reports taps; the real
 * shortcuts come later.
 */
public class DockView extends View {

    public interface Listener {
        void onTileTapped(String name);
    }

    private static final float W = 320f;
    private static final float H = 240f;

    private static final String[] TILES = {
        "MAP", "POKéMON", "BAG", "CARD", "POKéDEX", "POKéNAV", "SAVE", "OPTION"
    };
    private static final int[] ICONS = {
        0xFFE8C23A, 0xFFE04A3C, 0xFFD98A2B, 0xFF5A8CD8,
        0xFFD83A5E, 0xFFF08A24, 0xFF6CB4E4, 0xFF8C8C9E
    };
    private static final String[] SMALL = { "ITEM", "START" };

    // Theme (purple).
    private static final int FRAME = 0xFF4A2C7A;
    private static final int PANEL = 0xFFE8DEF7;
    private static final int OUTLINE = 0xFF3A2A55;
    private static final int TILE = 0xFFFAF8FD;
    private static final int TILE_HI = 0xFFC9A8F0;
    private static final int TEXT = 0xFF2A1E3D;
    private static final int FOOTER = 0xFF3A2A55;

    private final RectF[] tileRects = new RectF[TILES.length + SMALL.length];
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final String buildLabel;
    private Listener listener;
    private int litTile = -1;
    private String status = "Tap a tile";

    // Scale from the 320x240 design grid to the view.
    private float scale = 1f;
    private float offsetX = 0f;
    private float offsetY = 0f;

    public DockView(Context context, int buildNumber) {
        super(context);
        buildLabel = "build " + buildNumber;
        for (int i = 0; i < TILES.length; i++) {
            tileRects[i] = new RectF(210, 5 + i * 24, 316, 5 + i * 24 + 22);
        }
        tileRects[TILES.length] = new RectF(210, 200, 261, 236);
        tileRects[TILES.length + 1] = new RectF(265, 200, 316, 236);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        text.setTextAlign(Paint.Align.CENTER);
    }

    public void setListener(Listener l) {
        listener = l;
    }

    public void setStatus(String s) {
        status = s;
        invalidate();
    }

    private String tileName(int i) {
        return i < TILES.length ? TILES[i] : SMALL[i - TILES.length];
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        scale = Math.min(w / W, h / H);
        offsetX = (w - W * scale) / 2f;
        offsetY = (h - H * scale) / 2f;
    }

    private void box(Canvas c, RectF r, int color) {
        fill.setColor(OUTLINE);
        c.drawRoundRect(r, 4, 4, fill);
        fill.setColor(color);
        c.drawRoundRect(new RectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1), 3, 3, fill);
    }

    @Override
    protected void onDraw(Canvas c) {
        c.drawColor(Color.BLACK);
        c.save();
        c.translate(offsetX, offsetY);
        c.scale(scale, scale);

        fill.setColor(FRAME);
        c.drawRect(0, 0, W, H, fill);
        fill.setColor(PANEL);
        c.drawRoundRect(new RectF(2, 2, 204, 220), 5, 5, fill);
        fill.setColor(FOOTER);
        c.drawRect(2, 222, 204, 238, fill);

        text.setTextSize(11f);
        for (int i = 0; i < tileRects.length; i++) {
            RectF r = tileRects[i];
            box(c, r, i == litTile ? TILE_HI : TILE);
            float cx = r.centerX();
            if (i < TILES.length) {
                fill.setColor(OUTLINE);
                c.drawRoundRect(new RectF(r.left + 4, r.top + 4, r.left + 20, r.top + 18), 2, 2, fill);
                fill.setColor(ICONS[i]);
                c.drawRoundRect(new RectF(r.left + 5, r.top + 5, r.left + 19, r.top + 17), 2, 2, fill);
                cx = r.left + 64;
            }
            text.setColor(TEXT);
            c.drawText(tileName(i), cx, r.centerY() + 4, text);
        }

        text.setColor(TEXT);
        text.setTextSize(14f);
        c.drawText("DualDock", 103, 60, text);
        text.setTextSize(10f);
        c.drawText("Milestone 0: screen + touch test", 103, 80, text);
        text.setTextSize(12f);
        c.drawText(status, 103, 130, text);

        text.setColor(0xFFD0C4E0);
        text.setTextSize(9f);
        text.setTextAlign(Paint.Align.LEFT);
        c.drawText(buildLabel, 8, 233, text);
        text.setTextAlign(Paint.Align.CENTER);

        c.restore();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = (e.getX() - offsetX) / scale;
        float y = (e.getY() - offsetY) / scale;
        if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
            for (int i = 0; i < tileRects.length; i++) {
                if (tileRects[i].contains(x, y)) {
                    litTile = i;
                    status = "Tapped " + tileName(i);
                    invalidate();
                    if (listener != null) {
                        listener.onTileTapped(tileName(i));
                    }
                    return true;
                }
            }
            status = String.format("Touch at %.0f, %.0f", x, y);
            invalidate();
            return true;
        }
        if (e.getActionMasked() == MotionEvent.ACTION_UP) {
            litTile = -1;
            invalidate();
        }
        return true;
    }
}
