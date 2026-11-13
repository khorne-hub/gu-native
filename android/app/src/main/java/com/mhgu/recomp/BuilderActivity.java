package com.mhgu.recomp;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.StatFs;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.Arrays;

/**
 * "Build the game code on this phone" (Settings, Game folder; or the game folder screen when the game code is missing
 * or from another version): the player's own exefs (1.4.0) from the chosen folder makes the game code here, with no PC. BuilderService does the work; this screen shows what it uses, what it
 * needs (the download, the space, the time, the heat), and then how it goes, stage by stage, and what to do if it stops.
 * The same screen and texts as MH4U for Android's (for the files each game needs).
 *
 * The idea of building the game code on the phone is Klark231's (github.com/klark231/mh4u-builder, for MH4U); this is
 * the ports' own implementation of it.
 */
public class BuilderActivity extends Activity {
    static final String EXTRA_FOLDER = "folder";
    /** Started from the game's settings: the game is running and has to start again to load the new library. */
    static final String EXTRA_FROM_GAME = "from_game";

    private boolean es, fromGame;
    private File folder, main, romfs;
    private boolean version;  // (exefs/main is 1.4.0's)
    private static final byte[] BUILD_ID = {(byte) 0xfb, 0x08, (byte) 0xf1, (byte) 0xd2, 0x0f, (byte) 0xd1, 0x20, 0x4f};
    private LinearLayout stagesBox;
    private TextView summary, status, detail, rightTitle;
    private ProgressBar bar;
    private Button go, close, free;
    private final Runnable refresh = this::show;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = Look.es();
        fromGame = getIntent().getBooleanExtra(EXTRA_FROM_GAME, false);
        String path = getIntent().getStringExtra(EXTRA_FOLDER);
        if (path == null && !BuilderService.job.folder.isEmpty()) path = BuilderService.job.folder;  // (from the notification)
        if (path == null) path = BuilderTools.readText(new File(AppHome.home(this), "game_folder.txt"));
        folder = path.isEmpty() ? null : new File(path);
        look();
        build();
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        BuilderService.listen(refresh);
        show();
    }

    @Override
    protected void onPause() {
        super.onPause();
        BuilderService.unlisten(refresh);
    }

    /** The folder's exefs/main (its version) and romfs. */
    private void look() {
        main = romfs = null;
        version = false;
        if (folder == null) return;
        for (File w : new File[] {folder, new File(folder, "data")})
            if (new File(w, "exefs/main").isFile()) { folder = w; break; }
        File m = new File(folder, "exefs/main"), r = new File(folder, "romfs");
        if (m.isFile()) main = m;
        if (r.isDirectory()) romfs = r;
        if (main != null) {
            byte[] head = new byte[0x48];
            try (java.io.FileInputStream in = new java.io.FileInputStream(main)) {
                version = in.read(head) == head.length && head[0] == 'N' && head[1] == 'S' && head[2] == 'O' && head[3] == '0'
                        && Arrays.equals(Arrays.copyOfRange(head, 0x40, 0x48), BUILD_ID);
            } catch (Exception ignored) {
            }
        }
    }

    // ---- the screen
    private void build() {
        int pad = Look.dp(this, 14);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, Look.dp(this, 8), pad * 2, Look.dp(this, 6));
        TextView title = Look.text(this, es ? "CREAR EL CÓDIGO DEL JUEGO" : "BUILD THE GAME CODE", 20, Look.GOLD, true);
        title.setGravity(Gravity.CENTER);
        page.addView(title);
        TextView intro = Look.text(this, es
                ? "El código del juego se crea una vez, en este teléfono, desde tu propia copia. No se envía nada a ningún lado."
                : "The game's code is made once, on this phone, from your own copy. Nothing is sent anywhere.", 14, Look.CREAM, false);
        intro.setGravity(Gravity.CENTER);
        intro.setPadding(0, Look.dp(this, 2), 0, Look.dp(this, 8));
        page.addView(intro);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout left = panel(es ? "QUÉ USA" : "WHAT IT USES");
        summary = Look.text(this, "", 13, Look.CREAM, false);
        summary.setLineSpacing(Look.dp(this, 2), 1);
        left.addView(summary);
        // (the tools stay for the next build: this gives their space back; a later build downloads them again)
        free = Look.button(this, "", false);
        free.setOnClickListener(v -> {
            if (BuilderService.job.active()) return;
            long size = BuilderTools.used(this);
            Look.notice(this, es ? "BORRAR LAS HERRAMIENTAS" : "DELETE THE TOOLS", es
                            ? "Se borran las herramientas y lo que haya quedado de una creación (" + mb(size) + "). La librería que "
                              + "ya creaste queda en tu carpeta. Si vuelves a crearla, se descargan otra vez."
                            : "The tools and anything left from a build are deleted (" + mb(size) + "). The library you already "
                              + "made stays in your folder. If you build it again, they are downloaded again.",
                    new Look.Choice(es ? "Borrar" : "Delete", true, () -> {
                        BuilderTools.deleteTree(BuilderTools.root(this));
                        show();
                    }),
                    new Look.Choice(es ? "Cancelar" : "Cancel", false, null));
        });
        LinearLayout.LayoutParams fb = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 38));
        fb.setMargins(0, Look.dp(this, 6), 0, 0);
        left.addView(free, fb);
        columns.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        // (the right panel: what a build needs, before the first one; then its stages and how it goes, or why it stopped)
        LinearLayout right = panel("");
        rightTitle = (TextView) right.getChildAt(0);
        stagesBox = new LinearLayout(this);
        stagesBox.setOrientation(LinearLayout.VERTICAL);
        right.addView(stagesBox);
        detail = Look.text(this, "", 13, Look.CREAM, false);
        detail.setPadding(0, Look.dp(this, 6), 0, 0);
        detail.setLineSpacing(Look.dp(this, 2), 1);
        right.addView(detail);
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        gap.setMargins(Look.dp(this, 10), 0, 0, 0);
        columns.addView(right, gap);
        page.addView(columns);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(Look.WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(Look.GOLD));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(Look.BRONZE));
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Look.dp(this, 10));
        bp.setMargins(pad * 2, Look.dp(this, 4), pad * 2, 0);
        whole.addView(bar, bp);
        status = Look.text(this, "", 14, Look.GOLD, false);
        status.setGravity(Gravity.CENTER);
        status.setPadding(pad * 2, Look.dp(this, 4), pad * 2, Look.dp(this, 6));
        whole.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        go = Look.button(this, "", true);
        go.setOnClickListener(v -> press());
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 46));
        size.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(go, size);
        close = Look.button(this, "", false);
        close.setOnClickListener(v -> leave());
        row.addView(close, new LinearLayout.LayoutParams(size));
        row.setPadding(0, 0, 0, Look.dp(this, 10));
        whole.addView(row);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
        go.requestFocus();
    }

    private LinearLayout panel(String head) {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setBackground(Look.plate(this, Look.PANEL, 6));
        int pad = Look.dp(this, 12);
        p.setPadding(pad, Look.dp(this, 8), pad, Look.dp(this, 10));
        p.addView(Look.text(this, head, 13, Look.GOLD, true));
        return p;
    }

    private static String mb(long bytes) { return (bytes >> 20) + " MB"; }

    private long free() {
        try {
            return new StatFs(getFilesDir().getAbsolutePath()).getAvailableBytes();
        } catch (Exception e) {
            return Long.MAX_VALUE;
        }
    }

    /** The space a run needs now: the tools still missing, the work folder (it shrinks to nothing after a run). */
    private long needed() {
        long work = BuilderService.work(this).exists() ? BuilderTools.sizeOf(BuilderService.work(this)) : 0;
        return BuilderTools.toInstall(this) + Math.max(0, (1500L << 20) - work);
    }

    private void show() {
        BuilderService.Job j = BuilderService.job;
        boolean active = j.active();
        // what it uses
        SpannableStringBuilder s = new SpannableStringBuilder();
        line(s, es ? "Carpeta: " : "Folder: ", folder == null ? (es ? "ninguna elegida" : "none chosen") : folder.getAbsolutePath(), folder != null);
        line(s, "exefs/main: ", main == null ? (es ? "no está" : "missing") : version ? (es ? "versión 1.4.0" : "version 1.4.0")
                : (es ? "no es de la versión 1.4.0" : "not version 1.4.0"), main != null && version);
        line(s, "romfs: ", romfs == null ? (es ? "no está" : "missing") : (es ? "está" : "found"), romfs != null);
        line(s, es ? "Crea: " : "Makes: ", BuilderService.LIBRARY + es(" (+ .id), en la carpeta", " (+ .id), in the folder"), true);
        summary.setText(s);

        // the stages (once a build ran: before, the panel says what one needs)
        String interruptedRun = BuilderService.interrupted(this);
        boolean showStages = !j.status.equals("idle") || interruptedRun != null;
        rightTitle.setText(showStages ? (es ? "CÓMO VA" : "HOW IT GOES") : (es ? "QUÉ HACE FALTA" : "WHAT IT NEEDS"));
        stagesBox.setVisibility(showStages ? View.VISIBLE : View.GONE);
        stagesBox.removeAllViews();
        int at = Arrays.asList(BuilderService.STAGES).indexOf(j.stage);
        boolean ran = !j.status.equals("idle");
        for (int i = 0; i < BuilderService.STAGES.length; i++) {
            String name = BuilderService.STAGES[i];
            String mark;
            int color;
            if (!ran) { mark = "·"; color = Look.CREAM; }
            else if (j.status.equals("done") || i < at) { mark = "✓"; color = Look.GOLD; }
            else if (i == at && active) { mark = "▶"; color = Look.GOLD; }
            else if (i == at && j.status.equals("failed")) { mark = "✗"; color = Look.ALERT; }
            else { mark = "·"; color = Look.CREAM; }
            TextView t = Look.text(this, mark + "  " + BuilderText.stage(name, es)
                    + (name.equals("tools") && BuilderTools.toDownload(this) == 0 && (!ran || i < at) ? (es ? " (ya están)" : " (already here)") : ""),
                    13, color, i == at && active);
            t.setPadding(0, Look.dp(this, 3), 0, 0);
            stagesBox.addView(t);
        }

        // below: what it needs, or how it is going, or why it stopped
        long elapsed = System.currentTimeMillis() - j.started;
        String interrupted = BuilderService.interrupted(this);
        if (active) {
            detail.setTextColor(Look.CREAM);
            detail.setText((es ? "Tiempo: " : "Time: ") + BuilderText.minutes(elapsed, es) + eta(j)
                    + (es ? "\nSigue aunque apagues la pantalla o uses otra app. Deja el teléfono cargando."
                          : "\nIt goes on with the screen off or in another app. Keep the phone charging."));
        } else if (j.status.equals("failed")) {
            detail.setTextColor(Look.ALERT);
            detail.setText(BuilderText.failure(j.errorStage, j.errorText, es) + "\n\n" + (es ? "Detalle: " : "Details: ") + j.errorText);
        } else if (j.status.equals("done")) {
            detail.setTextColor(Look.CREAM);
            detail.setText((es ? "Listo en " : "Done in ") + BuilderText.minutes(elapsed, es) + ". " + (es
                    ? "El código del juego quedó en tu carpeta, junto a exefs y romfs."
                    : "The game code is in your folder, beside exefs and romfs."));
        } else {
            long dl = BuilderTools.toDownload(this), need = needed(), free = free();
            StringBuilder t = new StringBuilder();
            if (interrupted != null) t.append(es ? "La última creación se interrumpió: sigue desde donde quedó.\n\n"
                                                 : "The last build was interrupted: it goes on from where it stopped.\n\n");
            t.append(dl > 0 ? (es ? "• La primera vez descarga las herramientas, unos " + mb(dl) + ": Python (python.org) y Zig (PyPI), "
                                    + "software libre. Nunca descarga nada del juego.\n"
                                  : "• The first time it downloads the tools, about " + mb(dl) + ": Python (python.org) and Zig (PyPI), "
                                    + "free software. It never downloads anything of the game.\n")
                            : (es ? "• Las herramientas ya están en el teléfono.\n" : "• The tools are already on the phone.\n"));
            t.append(es ? "• Espacio: hacen falta " + mb(need) + " libres mientras se crea (hay " + mb(free) + "). Al terminar se "
                          + "borra lo temporal y quedan las herramientas (" + mb(BuilderTools.INSTALLED_SIZE) + ").\n"
                        : "• Space: it needs " + mb(need) + " free while it builds (there are " + mb(free) + "). When it ends the "
                          + "temporary files go and the tools stay (" + mb(BuilderTools.INSTALLED_SIZE) + ").\n");
            t.append(es ? "• Tiempo: de 20 minutos a 3 horas según el teléfono (es más grande que el de MH4U). Sigue con la "
                          + "pantalla apagada y, si se corta, sigue desde donde quedó.\n"
                        : "• Time: 20 minutes to 3 hours depending on the phone (it is bigger than MH4U's). It goes on with the "
                          + "screen off and, if it is cut, it goes on from where it stopped.\n");
            t.append(es ? "• El teléfono se calienta: déjalo cargando, sin funda si puedes. Caliente, va más despacio; muy caliente, "
                          + "se pausa solo hasta que se enfría."
                        : "• The phone gets warm: keep it charging, out of its case if you can. Warm, it goes slower; too hot, it "
                          + "pauses by itself until it cools down.");
            detail.setTextColor(Look.CREAM);
            detail.setText(t);
        }

        int percent = j.status.equals("done") ? 100 : ran ? BuilderText.percent(j) : 0;
        bar.setIndeterminate(active && percent < 0);
        bar.setProgress(Math.max(0, percent));
        bar.setVisibility(ran ? View.VISIBLE : View.INVISIBLE);
        status.setTextColor(j.status.equals("failed") ? Look.ALERT : Look.GOLD);
        status.setText(active ? BuilderText.status(j, es) + (percent >= 0 ? "  (" + percent + " %)" : "")
                : j.status.equals("failed") ? (es ? "Se detuvo en: " : "Stopped at: ") + BuilderText.stage(j.stage, es)
                : j.status.equals("cancelled") ? (es ? "Cancelada. Puedes seguir cuando quieras." : "Cancelled. You can go on whenever you like.")
                : j.status.equals("done") ? (es ? "El código del juego está listo." : "The game code is ready.")
                : missing());

        boolean ready = folder != null && main != null && version && romfs != null;
        if (active) {
            go.setText(es ? "Cancelar" : "Cancel");
            go.setEnabled(true);
        } else if (j.status.equals("done")) {
            go.setText(fromGame ? (es ? "Reiniciar el juego" : "Restart the game") : (es ? "Jugar" : "Play"));
            go.setEnabled(true);
        } else {
            go.setText(j.status.equals("failed") || j.status.equals("cancelled") || interrupted != null
                    ? (es ? "Seguir" : "Go on") : (es ? "Crear" : "Build"));
            go.setEnabled(ready);
        }
        close.setText(active ? (es ? "Ocultar" : "Hide") : (es ? "Volver" : "Back"));
        long used = active ? 0 : BuilderTools.used(this);
        free.setText((es ? "Borrar las herramientas (" : "Delete the tools (") + mb(used) + ")");
        free.setVisibility(used > (1 << 20) ? View.VISIBLE : View.GONE);
    }

    private String es(String spanish, String english) { return es ? spanish : english; }

    private String eta(BuilderService.Job j) {
        if (!j.stage.equals("compile") || j.done < 3 || j.total <= j.done) return "";
        long spent = System.currentTimeMillis() - j.stageStarted;
        long left = spent * (j.total - j.done) / j.done;
        return (es ? " · quedan unos " : " · about ") + BuilderText.minutes(left, es) + (es ? "" : " left");
    }

    private String missing() {
        if (folder == null) return es ? "Primero elige la carpeta del juego." : "Choose the game's folder first.";
        if (main == null || romfs == null)
            return es ? "En la carpeta faltan exefs o romfs." : "exefs or romfs is missing from the folder.";
        if (!version) return es ? "Este exefs no es de la versión 1.4.0." : "This exefs is not version 1.4.0.";
        return es ? "Toca Crear para empezar." : "Tap Build to start.";
    }

    private void line(SpannableStringBuilder s, String label, String value, boolean ok) {
        int at = s.length();
        s.append(label);
        s.setSpan(new ForegroundColorSpan(Look.GOLD), at, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        at = s.length();
        s.append(value).append("\n");
        s.setSpan(new ForegroundColorSpan(ok ? Look.CREAM : Look.ALERT), at, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }

    private void press() {
        BuilderService.Job j = BuilderService.job;
        if (j.active()) {
            Look.notice(this, es ? "¿CANCELAR?" : "CANCEL?", es
                            ? "Lo ya hecho se guarda: la próxima vez sigue desde donde quedó."
                            : "What is done is kept: the next time it goes on from where it stopped.",
                    new Look.Choice(es ? "Cancelar la creación" : "Cancel the build", true,
                            () -> startService(new Intent(this, BuilderService.class).setAction(BuilderService.ACTION_CANCEL))),  // (running: no foreground start needed)
                    new Look.Choice(es ? "Seguir creando" : "Keep building", false, null));
            return;
        }
        if (j.status.equals("done")) {
            j.status = "idle";
            if (fromGame) {  // (the game holds the old code: the app starts again, as Import save does)
                // (the game runs the copy in the app's own folder: without it, the start looks at the folder again and
                // copies the new one, GameFolderActivity.verify / keepLib)
                new File(getFilesDir(), "game/libmhgugame.so").delete();
                startActivity(new Intent(this, PhoenixActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        .putExtra(PhoenixActivity.EXTRA_PID, android.os.Process.myPid()));
                finish();
                return;
            }
            setResult(RESULT_OK);
            finish();
            return;
        }
        long need = needed(), free = free();
        if (free < need) {
            Look.notice(this, es ? "FALTA ESPACIO" : "NOT ENOUGH SPACE", es
                            ? "Hacen falta " + mb(need) + " libres en el teléfono y hay " + mb(free) + ". Libera espacio y vuelve a intentarlo."
                            : "It needs " + mb(need) + " free on the phone and there are " + mb(free) + ". Free some space and try again.",
                    new Look.Choice(es ? "Entendido" : "OK", true, null));
            return;
        }
        Intent run = new Intent(this, BuilderService.class).putExtra(BuilderService.EXTRA_GAME, folder.getAbsolutePath());
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(run);
        else startService(run);
    }

    private void leave() {
        if (BuilderService.job.status.equals("done") && !fromGame) setResult(RESULT_OK);
        finish();
    }

    @Override
    public void onBackPressed() { leave(); }
}
