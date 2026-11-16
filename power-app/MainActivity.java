package dev.s26.powerlogger;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public final class MainActivity extends Activity {
    private static final String BASE = "/data/local/tmp/s26-power-logger";
    private static final String CONTROL = "sh " + BASE + "/control.sh ";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private boolean visible, prepared;
    private int pendingRequests;
    private TextView state, usb, details, logPath, readingsTitle;
    private TextView voltage, batteryCurrent, systemCurrent, temperature, power;
    private EditText init, duration, interval;
    private Button start, stop;
    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            if (!visible) return;
            if (pendingRequests == 0) request(null);
            handler.postDelayed(this, 1000);
        }
    };

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(0, dp(4), 0, dp(4));
        return view;
    }
    private LinearLayout column() {
        LinearLayout view = new LinearLayout(this);
        view.setOrientation(LinearLayout.VERTICAL);
        return view;
    }
    private GradientDrawable background(int color) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color); drawable.setCornerRadius(dp(14));
        return drawable;
    }
    private EditText field(LinearLayout parent, String label, String key, int defaultValue) {
        parent.addView(text(label, 13, 0xff536176));
        EditText input = new EditText(this);
        input.setSingleLine(true); input.setInputType(InputType.TYPE_CLASS_NUMBER);
        input.setText(String.valueOf(getPreferences(0).getInt(key, defaultValue)));
        input.setTextSize(20); input.setContentDescription(label);
        parent.addView(input);
        return input;
    }
    private TextView metric(LinearLayout row, String label) {
        LinearLayout card = column();
        card.setPadding(dp(14), dp(12), dp(14), dp(12));
        card.setBackground(background(Color.WHITE));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1);
        params.setMargins(0, dp(6), dp(8), dp(6)); row.addView(card, params);
        TextView caption = text(label, 12, 0xff536176);
        caption.setMinHeight(dp(40)); card.addView(caption);
        TextView value = text("—", 25, 0xff142640);
        value.setMaxLines(1);
        value.setAutoSizeTextTypeUniformWithConfiguration(16, 25, 1, android.util.TypedValue.COMPLEX_UNIT_SP);
        value.setTypeface(null, Typeface.BOLD); card.addView(value);
        return value;
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xfff2f5fa);
        LinearLayout page = column();
        page.setPadding(dp(24), dp(24), dp(24), dp(24));
        page.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(dp(24), dp(16) + insets.getSystemWindowInsetTop(), dp(24), dp(20) + insets.getSystemWindowInsetBottom());
            return insets;
        });
        scroll.addView(page); setContentView(scroll);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        TextView title = text("S26 Power", 30, 0xff142640); title.setTypeface(null, Typeface.BOLD); page.addView(title);
        page.addView(text("Local capture for Monsoon experiments", 14, 0xff536176));
        state = text("Connecting to collector…", 22, 0xff185cba); page.addView(state);
        usb = text("Root access is required for the collector.", 14, 0xff536176); page.addView(usb);

        LinearLayout times = new LinearLayout(this);
        LinearLayout left = column(), right = column();
        times.addView(left, new LinearLayout.LayoutParams(0, -2, 1));
        times.addView(right, new LinearLayout.LayoutParams(0, -2, 1));
        init = field(left, "Init delay (seconds)", "init", 10);
        duration = field(right, "Run duration (seconds)", "duration", 60);
        page.addView(times);
        interval = field(page, "Sample interval (ms)", "interval", 1000);
        LinearLayout buttons = new LinearLayout(this);
        start = new Button(this); start.setText("Start"); start.setTextColor(Color.WHITE); start.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff185cba));
        stop = new Button(this); stop.setText("Stop"); stop.setEnabled(false);
        buttons.addView(start, new LinearLayout.LayoutParams(0, dp(54), 1));
        buttons.addView(stop, new LinearLayout.LayoutParams(0, dp(54), 1));
        page.addView(buttons);
        details = text("Init time lets you unplug USB before the run window. Normal sleep is preserved.", 13, 0xff536176); page.addView(details);

        readingsTitle = text("Readings", 18, 0xff142640); page.addView(readingsTitle);
        LinearLayout row1 = new LinearLayout(this), row2 = new LinearLayout(this);
        voltage = metric(row1, "Battery interface voltage"); batteryCurrent = metric(row1, "Battery interface current");
        systemCurrent = metric(row2, "System current · ISYS"); temperature = metric(row2, "Battery sensor temperature");
        page.addView(row1); page.addView(row2);
        power = text("Battery-interface V × I: —", 15, 0xff142640); page.addView(power);
        page.addView(text("V × I is derived, not calibrated whole-device power. Init rows and run rows are labeled separately. Sleep gaps are marked in the CSV.", 12, 0xff536176));
        logPath = text("No capture yet", 12, 0xff536176); logPath.setTextIsSelectable(true); page.addView(logPath);
        page.addView(text("After Start, launch your workload and leave this screen. The display and live updates add power overhead. The collector keeps working without USB or this app open.", 12, 0xff536176));
        start.setOnClickListener(v -> {
            try {
                int a = Integer.parseInt(init.getText().toString()), b = Integer.parseInt(duration.getText().toString()), c = Integer.parseInt(interval.getText().toString());
                if (a < 0 || a > 86400 || b < 1 || b > 86400 || c < 250 || c > 60000) throw new NumberFormatException();
                getPreferences(0).edit().putInt("init", a).putInt("duration", b).putInt("interval", c).apply();
                request("start " + a + " " + b + " " + c);
            } catch (NumberFormatException error) {
                Toast.makeText(this, "Init 0–86400 s; run 1–86400 s; sample 250–60000 ms", Toast.LENGTH_LONG).show();
            }
        });
        stop.setOnClickListener(v -> request("stop"));
    }

    @Override protected void onResume() { super.onResume(); visible = true; handler.post(refresh); }
    @Override protected void onPause() { visible = false; handler.removeCallbacks(refresh); super.onPause(); }
    @Override protected void onDestroy() { handler.removeCallbacks(refresh); worker.shutdownNow(); super.onDestroy(); }

    private String root(String command) throws Exception {
        Process process;
        try { process = new ProcessBuilder("su", "-c", command).redirectErrorStream(true).start(); }
        catch (java.io.IOException error) { throw new Exception("Enable S26 Power in KernelSU > Superuser, then reopen this app."); }
        try {
            if (!process.waitFor(25, TimeUnit.SECONDS)) throw new Exception("Root request timed out. Allow S26 Power in KernelSU.");
            StringBuilder output = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line; while ((line = reader.readLine()) != null) output.append(line).append('\n');
            }
            if (process.exitValue() != 0) throw new Exception(output.length() > 0 ? output.toString().trim() : "Root access denied. Allow S26 Power in KernelSU.");
            return output.toString();
        } finally { process.destroy(); }
    }

    private void prepare() throws Exception {
        for (String name : new String[]{"power-logger", "control.sh"}) {
            File target = new File(getFilesDir(), name);
            try (InputStream source = getAssets().open(name); FileOutputStream destination = new FileOutputStream(target)) {
                byte[] buffer = new byte[8192]; int count;
                while ((count = source.read(buffer)) != -1) destination.write(buffer, 0, count);
            }
        }
        String source = getFilesDir().getAbsolutePath();
        root("if [ ! -x " + BASE + "/power-logger ] || [ ! -f " + BASE + "/control.sh ]; then "
                + "mkdir -p " + BASE + " && cp '" + source + "/power-logger' " + BASE + "/power-logger && cp '" + source + "/control.sh' " + BASE + "/control.sh && chmod 755 " + BASE + "/power-logger; fi");
        prepared = true;
    }

    private void request(String action) {
        if (action == null && pendingRequests != 0) return;
        ++pendingRequests;
        worker.execute(() -> {
            try {
                if (!prepared) prepare();
                if (action != null) root(CONTROL + action);
                String status = root(CONTROL + "status");
                handler.post(() -> { --pendingRequests; if (!isDestroyed()) display(status); });
            } catch (Exception error) {
                handler.post(() -> {
                    --pendingRequests;
                    if (!isDestroyed()) {
                        state.setText("Collector unavailable"); details.setText(error.getMessage());
                        start.setEnabled(true); stop.setEnabled(false);
                    }
                });
            }
        });
    }

    private static double number(Map<String, String> values, String key) {
        try { return Double.parseDouble(values.get(key)); } catch (Exception ignored) { return Double.NaN; }
    }
    private static String value(Map<String, String> row, String key, double scale, String unit, boolean signed) {
        double x = number(row, key) / scale;
        return Double.isNaN(x) ? "—" : String.format(Locale.US, signed ? "%+.2f %s" : "%.2f %s", x, unit);
    }
    private void display(String output) {
        Map<String, String> status = new HashMap<>(), row = new HashMap<>();
        for (String line : output.split("\n")) {
            int split = line.indexOf('='); if (split > 0) status.put(line.substring(0, split), line.substring(split + 1));
        }
        if (!status.containsKey("STATE")) {
            state.setText("Collector unavailable"); details.setText(output.trim()); return;
        }
        if (status.containsKey("CSV_HEADER") && status.containsKey("CSV_LAST")) {
            String[] keys = status.get("CSV_HEADER").split(",", -1), values = status.get("CSV_LAST").split(",", -1);
            if (keys.length == values.length) for (int i = 0; i < keys.length; i++) row.put(keys[i], values[i]);
        }
        boolean running = "running".equals(status.get("STATE"));
        start.setEnabled(!running); stop.setEnabled(running);
        init.setEnabled(!running); duration.setEnabled(!running); interval.setEnabled(!running);
        if (running) {
            init.setText(status.get("init_seconds"));
            duration.setText(status.get("run_seconds"));
            interval.setText(status.get("interval_ms"));
        }
        double elapsed = number(row, "elapsed_seconds");
        double initTime = number(status, "init_seconds"), runTime = number(status, "run_seconds");
        boolean initializing = "init".equals(row.get("phase"));
        if (running) state.setText(String.format(Locale.US, initializing ? "Starting in %.0f s" : "Recording · %.0f s left",
                Math.max(0, Math.ceil((initializing ? initTime : initTime + runTime) - elapsed))));
        else state.setText("duration".equals(status.get("stop_reason")) ? "Capture complete" : "Ready");
        usb.setText("0".equals(row.get("usb_online")) ? "USB disconnected" : ("1".equals(row.get("usb_online")) ? "USB connected" : "USB state unavailable"));
        usb.setTextColor("0".equals(row.get("usb_online")) ? 0xff167447 : 0xff7e5812);
        readingsTitle.setText(running ? "Live readings" : "Last captured readings");
        voltage.setText(value(row, "battery_voltage_uv", 1e6, "V", false));
        batteryCurrent.setText(value(row, "battery_current_ua", 1000, "mA", true));
        systemCurrent.setText(value(row, "system_current_ma", 1, "mA", false));
        temperature.setText(value(row, "battery_temp_deci_c", 10, "°C", false));
        power.setText("Battery-interface V × I: " + value(row, "battery_interface_power_w", 1, "W", true));
        if (!row.isEmpty() && !"0".equals(row.get("read_error_mask"))) details.setText("Some sensor reads failed; missing values are blank in the CSV.");
        else if ("1".equals(row.get("sampling_gap"))) details.setText(String.format(Locale.US, "Sampling gap: %.1f s. Normal sleep is preserved.", number(row, "sample_gap_ns") / 1e9));
        else details.setText(running ? "Local logging is active. USB and this app can be disconnected or closed." : "Init time lets you unplug USB before the run window. Normal sleep is preserved.");
        logPath.setText(status.containsKey("FILE") ? "CSV on phone\n" + status.get("FILE") : "No capture yet");
    }
}
