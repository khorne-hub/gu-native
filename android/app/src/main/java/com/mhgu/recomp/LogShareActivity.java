package com.mhgu.recomp;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Compartir el registro: con el juego cerrado por un error (GameActivity.crashReport), o desde Ajustes.
 * Corre en su propio proceso (":share", como LogProvider), porque el del juego muere tras un error y lo que comparte
 * tiene que seguir disponible para la app que lo reciba. Junta en un archivo los datos del teléfono, el registro de esta
 * sesión y el de la anterior: el jugador elige a quién enviarlo; la app no manda nada por su cuenta.
 * (1.0.1) Diag.bundle arma el archivo, en un hilo propio: el teléfono y la app ahora, cómo terminaron sus últimos
 * procesos, los ajustes, los mods, las dos sesiones anteriores, el último armado y el registro del sistema.
 */
public class LogShareActivity extends Activity {
    static final String EXTRA_CRASH = "crash";
    static final String EXTRA_WHY = "why";  // (what went wrong, when the runtime knows it: "file_cut")
    private static final String TAG = "MHGU";
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int MAX_LOG = 2 << 20;  // (the end of each log: a debug session's can be large)

    private boolean es;
    private Thread systemLines;  // (after a crash) addSystemLines
    private Button shareButton;
    private boolean sharing;

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    /** The player's own GPU driver in use (files/gpu_drivers/selected), as DriverActivity shows it; null: the system's. */
    private String ownDriver() {
        File root = new File(getFilesDir(), "gpu_drivers");
        String folder = firstLine(new File(root, "selected"));
        if (folder == null || folder.isEmpty()) return null;
        String shown = firstLine(new File(new File(root, folder), "driver.txt"));
        return shown != null && !shown.isEmpty() ? shown : folder;
    }

    private static String firstLine(File f) {
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String s = r.readLine();
            return s == null ? null : s.trim();
        } catch (IOException e) {
            return null;
        }
    }

    private TextView text(String s, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private GradientDrawable plate(int fill) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setStroke(dp(2), EDGE);
        g.setCornerRadius(dp(6));
        return g;
    }

    private Button button(String label, boolean strong) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(strong ? GOLD : CREAM);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        b.setBackground(plate(BRONZE));
        return b;
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = "es".equals(Locale.getDefault().getLanguage());
        boolean crash = getIntent().getBooleanExtra(EXTRA_CRASH, false);
        if (crash && saved == null) {  // (once: not again when the screen turns)
            systemLines = new Thread(this::addSystemLines, "mhgu-system-log");
            systemLines.start();
        }

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(16), dp(24), dp(18));
        box.setBackground(plate(WINE));
        box.addView(text(crash ? (es ? "El juego se cerró por un error" : "The game stopped because of an error")
                               : (es ? "Compartir registro" : "Share log"), 19, GOLD, true));
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                                      LinearLayout.LayoutParams.WRAP_CONTENT);
        gap.setMargins(0, dp(12), 0, 0);
        // (runtime/hle/abort.c: the game's file system gave up on one of its files, GU#232) what to do, before the log
        if (crash && "file_cut".equals(getIntent().getStringExtra(EXTRA_WHY)))
            box.addView(text(es ? "Uno de los archivos del juego es más corto de lo que debería, o está dañado: se cortó al "
                                  + "copiarlo. Vuelve a copiar la carpeta romfs al dispositivo. En el juego, Sistema › Tu "
                                  + "partida › Verificar archivos del juego dice cuál es."
                                : "One of the game's files is shorter than it should be, or damaged: it was cut short when it "
                                  + "was copied. Copy the romfs folder to the device again. In the game, System › Your game › "
                                  + "Verify game files names it.", 15, GOLD, false), gap);
        // (1.0.2, S12: a driver of the player's own, any GPU, files/gpu_drivers/selected: named, with its version, so the
        // reports made with one are known; DriverActivity's driver.txt has the name it shows)
        if (crash) {
            String driver = ownDriver();
            box.addView(text(driver == null ? (es ? "Driver de GPU: el del sistema." : "GPU driver: the system's.")
                                            : (es ? "Driver de GPU: " + driver + " (uno tuyo, no el del sistema). Si el juego "
                                                    + "no arranca con él, la app vuelve sola al del sistema."
                                                  : "GPU driver: " + driver + " (one of your own, not the system's). If the "
                                                    + "game doesn't start with it, the app goes back to the system's by itself."),
                             14, driver == null ? CREAM : GOLD, false), gap);
        }
        box.addView(text(es ? "El registro explica qué pasó y en qué teléfono: el modelo y su sistema, la memoria, la batería y "
                              + "la temperatura, el tipo de red (Wi-Fi o datos, nunca su nombre), los ajustes y los mods de "
                              + "la app, y las últimas sesiones. Puedes enviarlo a quien te esté ayudando: se abre la lista "
                              + "de apps y tú eliges a dónde va. No lleva tu partida, cuentas, correo ni ubicación, pero "
                              + "puede incluir la dirección de tu red local si jugaste en LAN."
                            : "The log says what happened and on which phone: its model and system, the memory, the "
                              + "battery and the heat, the network's kind (Wi-Fi or mobile data, never its name), the app's "
                              + "settings and mods, and the last sessions. You can send it to whoever is helping you: the "
                              + "list of apps opens and you choose where it goes. Your save, accounts, e-mail and location "
                              + "are not in it, but it can hold your local network address if you played over LAN.",
                         15, CREAM, false), gap);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, dp(18), 0, 0);
        Button share = button(es ? "Compartir registro" : "Share log", true);
        share.setOnClickListener(v -> share());
        shareButton = share;
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(dp(190), dp(46));
        size.setMargins(dp(8), 0, dp(8), 0);
        row.addView(share, size);
        Button close = button(es ? "Cerrar" : "Close", false);
        close.setOnClickListener(v -> finish());
        row.addView(close, new LinearLayout.LayoutParams(size));
        box.addView(row);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(PANEL);
        scroll.setFillViewport(true);
        LinearLayout page = new LinearLayout(this);
        page.setGravity(Gravity.CENTER);
        page.setPadding(dp(32), dp(16), dp(32), dp(16));
        int room = getResources().getDisplayMetrics().widthPixels - dp(64);  // (never wider than the screen)
        page.addView(box, new LinearLayout.LayoutParams(Math.min(dp(520), room), LinearLayout.LayoutParams.WRAP_CONTENT));
        scroll.addView(page);
        setContentView(scroll);
    }

    // (1.0.1) the file is made on a thread of its own (the system log, the folders' sizes and the snapshot take a moment),
    // then the list of apps opens
    private void share() {
        if (sharing) return;
        sharing = true;
        final String label = shareButton.getText().toString();
        shareButton.setEnabled(false);
        shareButton.setText(es ? "Preparando…" : "Preparing…");
        final boolean crash = getIntent().getBooleanExtra(EXTRA_CRASH, false);
        new Thread(() -> {
            waitSystemLines();
            String header = device() + "share log: " + (crash ? "the error screen" : "from the settings") + ", "
                            + new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new java.util.Date()) + "\n";
            File report;
            try {
                report = Diag.bundle(this, header);
            } catch (Throwable t) {  // (never seen: the logs as before rather than no file)
                Log.e(TAG, "share log: " + t);
                report = Diag.plain(this, header, t);
            }
            final File shared = report;
            runOnUiThread(() -> {
                sharing = false;
                shareButton.setEnabled(true);
                shareButton.setText(label);
                if (isFinishing() || isDestroyed()) return;
                boolean zip = shared.getName().endsWith(".zip");
                Uri uri = LogProvider.uri(zip);
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType(zip ? "application/zip" : "text/plain");
                send.putExtra(Intent.EXTRA_SUBJECT, "MHGU log");
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.setClipData(ClipData.newRawUri("MHGU log", uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(Intent.createChooser(send, es ? "Compartir registro" : "Share log"));
            });
        }, "mhgu-share").start();
    }

    /**
     * After a crash: the system log's warnings and errors of this app (an app reads only its own) at the end of the
     * crashed session's log.txt, once the game's process is gone, as the MH4U port does. They say what the game's own
     * report can't: why the allocator or the C++ library stopped it, or the GPU driver's own words.
     */
    private void addSystemLines() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        String game = getPackageName();
        for (int i = 0; i < 30; i++) {  // (3 s at most)
            boolean alive = false;
            java.util.List<ActivityManager.RunningAppProcessInfo> running = am.getRunningAppProcesses();
            if (running != null)
                for (ActivityManager.RunningAppProcessInfo p : running) alive |= game.equals(p.processName);
            if (!alive) break;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return;
            }
        }
        java.util.List<String> lines = Diag.systemLog(this, 120, true);
        StringBuilder s = new StringBuilder("\n===== the system log's errors of this app (logcat, the last ").append(lines.size()).append(") =====\n");
        for (String l : lines) s.append(l).append('\n');
        for (String l : Diag.exits(this, 2)) s.append(l).append('\n');
        try (OutputStream out = new FileOutputStream(new File(AppHome.home(this), "log.txt"), true)) {
            out.write(s.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.e(TAG, "system log: " + e);
        }
    }

    /** addSystemLines done (or 4 s gone): before the log is shared. */
    private void waitSystemLines() {
        if (systemLines == null) return;
        try {
            systemLines.join(4000);
        } catch (InterruptedException ignored) {
        }
    }

    /** The phone, for whoever reads the log: nothing that names its owner. */
    private String device() {
        StringBuilder s = new StringBuilder("MHGU log\n");
        s.append("device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE).append(")\n");
        s.append("Android ").append(Build.VERSION.RELEASE).append(", API ").append(Build.VERSION.SDK_INT).append('\n');
        if (Build.VERSION.SDK_INT >= 31) s.append("chip: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        s.append("hardware: ").append(Build.HARDWARE).append(", ").append(Build.BOARD).append('\n');
        ActivityManager.MemoryInfo mem = new ActivityManager.MemoryInfo();
        ((ActivityManager) getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(mem);
        s.append("memory: ").append(mem.totalMem >> 20).append(" MB\n");
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {  // (one of each: the core types)
            java.util.LinkedHashSet<String> seen = new java.util.LinkedHashSet<>();
            for (String line; (line = r.readLine()) != null; )
                if (line.startsWith("Features") || line.startsWith("CPU part") || line.startsWith("CPU implementer")) seen.add(line.replaceAll("\\s+", " "));
            for (String line : seen) s.append(line).append('\n');
        } catch (IOException e) {
            s.append("no /proc/cpuinfo\n");
        }
        return s.toString();
    }
}
