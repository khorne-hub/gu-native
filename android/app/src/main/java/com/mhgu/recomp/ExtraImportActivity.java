package com.mhgu.recomp;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.Arrays;
import java.util.Locale;

/**
 * Settings -> Import DLC and event quests (S37): the player's own event quests and downloads, from a folder they choose.
 * MH4U for Android has the same screen, name and texts (its event quests are files of their own).
 *
 * MHGU keeps what its Download menu brought (event and challenge quests, item packs, Palicos) inside the save, in the
 * shared part of the file system (runtime/services/bonus.c has the layout). That menu can't reach its server from the
 * port, so the downloads come from a save that has them: the player's own, from an emulator or a backup of their console.
 * Chosen here, its file is checked and copied as the bonus content's "donor", MHGURecomp/bonus/system; at the next start
 * the runtime copies its downloads into the save, only when the save has none of its own (nothing else in the file is
 * touched). The save is copied to save_backups/<date>-dlc first. The app includes none and downloads none.
 *
 * The game's add-on content files (a DLC's RomFS with DLC.dlcp) are not used this way: the game takes them only through
 * its Download menu, which stops without its online services (Settings shows Add DLC only in a test run, MHGU_DLC=1).
 */
public class ExtraImportActivity extends Activity {
    private static final String TAG = "MHGU";
    // the save file (SaveFolder's): its size, and where its downloads are (runtime/services/bonus.c)
    private static final int SIZE = 5159100, HDR = 0x24, AREA = 0xc000, EVENTS = 0xf899, CHALLENGES = 0x127899, SLOT = 0x1c00;

    private boolean es;
    private TextView status;
    private Button main, leave;
    private File found;  // the save file chosen, checked (null: none yet)

    static File home(Context c) { return AppHome.home(c); }
    static File donor(Context c) { return new File(home(c), "MHGURecomp/bonus/system"); }

    /** What a save file holds of the Download menu's: event quests, challenge quests; null: not a save, or none. */
    static int[] downloads(File f) {
        if (!SaveFolder.isSave(f)) return null;
        byte[] s = new byte[SIZE];
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            r.readFully(s);
        } catch (IOException e) {
            return null;
        }
        long slot1 = (s[0x34] & 0xffL | (s[0x35] & 0xffL) << 8 | (s[0x36] & 0xffL) << 16 | (s[0x37] & 0xffL) << 24) + HDR;
        if (slot1 <= CHALLENGES + 40L * SLOT || slot1 >= SIZE) return null;
        boolean any = false;
        for (int i = AREA; i < slot1 && !any; i++) any = s[i] != 0;
        if (!any) return new int[] {0, 0};
        int[] n = new int[2];
        for (int i = 0; i < 160; i++) if (word(s, EVENTS + i * SLOT) != 0) n[0]++;
        for (int i = 0; i < 40; i++) if (word(s, CHALLENGES + i * SLOT) != 0) n[1]++;
        if (n[0] + n[1] == 0) n[0] = -1;  // (item packs or Palicos only)
        return n;
    }

    private static int word(byte[] s, int at) {
        return s[at] & 0xff | (s[at + 1] & 0xff) << 8 | (s[at + 2] & 0xff) << 16 | (s[at + 3] & 0xff) << 24;
    }

    // ---- the screen
    private LinearLayout column(String title, String body, boolean framed) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        if (framed) {
            c.setBackground(Look.plate(this, Look.PANEL, 6));
            c.setPadding(Look.dp(this, 14), Look.dp(this, 10), Look.dp(this, 14), Look.dp(this, 10));
        } else {
            c.setPadding(Look.dp(this, 18), 0, 0, 0);
        }
        c.addView(Look.text(this, title, 13, Look.GOLD, true));
        TextView t = Look.text(this, body, 13, Look.CREAM, false);
        t.setPadding(0, Look.dp(this, 4), 0, 0);
        t.setLineSpacing(Look.dp(this, 1), 1);
        c.addView(t);
        return c;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = Look.es();
        int pad = Look.dp(this, 14);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, Look.dp(this, 8), pad * 2, Look.dp(this, 6));

        TextView title = Look.text(this, es ? "IMPORTAR DLC Y MISIONES DE EVENTO" : "IMPORT DLC AND EVENT QUESTS", 20, Look.GOLD, true);
        title.setGravity(Gravity.CENTER);
        page.addView(title);
        TextView intro = Look.text(this, es ? "Trae las misiones de evento que ya tienes, de un emulador o de una copia de tu consola."
                                            : "Bring the event quests you already have, from an emulator or a backup of your console.", 14, Look.CREAM, false);
        intro.setGravity(Gravity.CENTER);
        intro.setPadding(0, Look.dp(this, 2), 0, Look.dp(this, 8));
        page.addView(intro);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        columns.addView(column(es ? "QUÉ CARPETA ELEGIR" : "WHICH FOLDER TO CHOOSE", es
                ? "Las descargas (misiones de evento y de desafío, packs de objetos, Felynes) vienen dentro de una partida: el archivo system de una que ya las tenga.\n"
                  + "• Ryujinx: clic derecho en el juego, Open User Save Directory.\n"
                  + "• yuzu y sus sucesores: clic derecho en el juego, Open Save Data Location.\n"
                  + "Copia ese archivo al teléfono y elige la carpeta donde lo pusiste."
                : "The downloads (event and challenge quests, item packs, Palicos) come inside a save: the system file of one that has them.\n"
                  + "• Ryujinx: right-click the game, Open User Save Directory.\n"
                  + "• yuzu and its successors: right-click the game, Open Save Data Location.\n"
                  + "Copy that file to the phone and choose the folder you put it in.", true),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        columns.addView(column(es ? "QUÉ VA A PASAR" : "WHAT WILL HAPPEN", es
                ? "• Tus cazadores quedan: solo las descargas pasan a tu partida, y solo si todavía no tiene ninguna.\n"
                  + "• Tu partida queda guardada en una copia, en MHGURecomp/save_backups.\n"
                  + "• El juego se reinicia: guarda antes lo que no quieras perder."
                : "• Your hunters stay: only the downloads go into your save, and only if it has none yet.\n"
                  + "• Your save is kept in a copy, in MHGURecomp/save_backups.\n"
                  + "• The game restarts: save first if you have progress to keep.", false),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        page.addView(columns);
        TextView own = Look.text(this, es ? "La app no incluye ni descarga DLC ni misiones de evento: los traes tú, bajo tu responsabilidad."
                                          : "The app includes no DLC or event quests and downloads none: you bring your own, at your own risk.",
                12, Look.CREAM, false);
        own.setGravity(Gravity.CENTER);
        own.setAlpha(.75f);
        own.setPadding(0, Look.dp(this, 10), 0, 0);
        page.addView(own);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(Look.WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        status = Look.text(this, "", 14, Look.GOLD, false);
        status.setGravity(Gravity.CENTER);
        status.setPadding(pad * 2, Look.dp(this, 4), pad * 2, Look.dp(this, 4));
        status.setVisibility(View.GONE);
        whole.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, 0, 0, Look.dp(this, 10));
        main = Look.button(this, es ? "Elegir carpeta" : "Choose a folder", true);
        main.setOnClickListener(v -> { if (found != null) confirm(); else browse(); });
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 46));
        size.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(main, size);
        leave = Look.button(this, es ? "Cancelar" : "Cancel", false);
        leave.setOnClickListener(v -> finish());
        LinearLayout.LayoutParams size2 = new LinearLayout.LayoutParams(Look.dp(this, 120), Look.dp(this, 46));
        size2.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(leave, size2);
        whole.addView(row);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {  // (the buttons stay above the system's bar)
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
        main.requestFocus();  // (a controller: A presses it)
    }

    private void say(String s, boolean bad) {
        status.setTextColor(bad ? Look.ALERT : Look.GOLD);
        status.setText(s);
        status.setVisibility(View.VISIBLE);
    }

    private void browse() {
        if (Build.VERSION.SDK_INT >= 30 ? !Environment.isExternalStorageManager()
                : checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            say(es ? "El juego no tiene permiso para leer tus carpetas: elige otra vez la carpeta del juego en Ajustes para darlo."
                   : "The game has no permission to read your folders: choose the game folder again in Settings to give it.", true);
            return;
        }
        FolderBrowser.open(this, null, es ? "ELIGE LA CARPETA DE LA PARTIDA CON DESCARGAS" : "CHOOSE THE FOLDER OF THE SAVE WITH DOWNLOADS",
                dir -> SaveFolder.isSave(new File(dir, "system")),
                (dir, holds) -> holds ? (es ? "✓ Aquí hay una partida (el archivo system)." : "✓ There is a save here (the system file).")
                                      : (es ? "Entra a la carpeta donde está el archivo system de la partida con las descargas."
                                            : "Go into the folder with the system file of the save that has the downloads."),
                this::check, null, false);
    }

    /** The save file in a folder chosen: in it, or a couple of levels down (an emulator's save folder's own). */
    private static File find(File dir, int depth) {
        if (dir == null || depth > 3) return null;
        File here = new File(dir, "system");
        if (SaveFolder.isSave(here)) return here;
        if (depth >= 3) return null;
        File[] kids = dir.listFiles(f -> f.isDirectory() && !f.getName().startsWith("."));
        if (kids == null || kids.length > 40) return null;
        Arrays.sort(kids, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));  // (the newest first)
        for (File k : kids) {
            File f = find(k, depth + 1);
            if (f != null) return f;
        }
        return null;
    }

    /** Does a DLC's own file (DLC.dlcp) sit in this folder or the ones under it? (to say why it isn't taken) */
    private static boolean dlcFiles(File dir, int depth) {
        if (dir == null || depth > 3) return false;
        File[] kids = dir.listFiles();
        if (kids == null) return false;
        for (File k : kids) if (k.isFile() && k.getName().equalsIgnoreCase("DLC.dlcp")) return true;
        if (kids.length > 60) return false;
        for (File k : kids) if (k.isDirectory() && dlcFiles(k, depth + 1)) return true;
        return false;
    }

    private void check(File dir) {
        found = null;
        main.setEnabled(false);
        leave.setEnabled(false);
        say(es ? "Revisando la carpeta..." : "Checking the folder...", false);
        new Thread(() -> {
            File f = find(dir, 0);
            int[] n = f != null ? downloads(f) : null;
            String wrong = problem(dir, f, n);
            runOnUiThread(() -> {
                main.setEnabled(true);
                leave.setEnabled(true);
                if (wrong != null) {
                    say(wrong, true);
                    main.setText(es ? "Elegir carpeta" : "Choose a folder");
                    return;
                }
                found = f;
                String what = n[0] < 0 ? (es ? "packs de objetos o Felynes" : "item packs or Palicos")
                        : es ? n[0] + " misiones de evento y " + n[1] + " de desafío" : n[0] + " event quests and " + n[1] + " challenge quests";
                say((es ? "Partida con descargas: " : "A save with downloads: ") + what
                    + (es ? ". Al continuar pasan a tu partida y el juego se reinicia." : ". Going on brings them into your save and restarts the game."), false);
                main.setText(es ? "Importar y reiniciar" : "Import and restart");
                main.requestFocus();
            });
        }).start();
    }

    /** What is wrong with the save found (or its absence), or null. */
    private String problem(File dir, File f, int[] n) {
        if (f == null)
            return dlcFiles(dir, 0)
                    ? (es ? "Ahí hay archivos de DLC (DLC.dlcp): el juego solo los toma con su menú Descargas, que necesita sus servicios en línea. Trae una partida que ya tenga las descargas."
                          : "There are DLC files there (DLC.dlcp): the game only takes them through its Download menu, which needs its online services. Bring a save that already has the downloads.")
                    : (es ? "En esa carpeta no hay una partida de MHGU. Elige la que tiene el archivo system."
                          : "There is no MHGU save in that folder. Choose the one with the system file.");
        if (n == null)
            return es ? "Esa partida no se pudo leer. Cópiala a la memoria del teléfono y vuelve a intentarlo."
                      : "That save could not be read. Copy it to the phone's storage and try again.";
        if (n[0] == 0 && n[1] == 0)
            return es ? "Esa partida no tiene descargas (misiones de evento, packs de objetos, Felynes)."
                      : "That save has no downloads (event quests, item packs, Palicos).";
        File mine = new File(SaveFolder.active(this), "system");
        if (mine.getAbsolutePath().equals(f.getAbsolutePath()))
            return es ? "Esa es la partida que el juego usa ahora." : "That is the save the game uses now.";
        int[] have = downloads(mine);
        if (have != null && (have[0] != 0 || have[1] != 0))
            return es ? "Tu partida ya tiene descargas propias: el juego las conserva y no toma otras."
                      : "Your save already has downloads of its own: the game keeps them and doesn't take others.";
        if (bonusOff())
            return es ? "Primero activa Contenido extra (Juego) y guarda: sin eso el juego no las toma."
                      : "First turn on Bonus content (Gameplay) and save: without it the game doesn't take them.";
        return null;
    }

    /** [Bonus Content] BonusContent = Off in mhgurecomp.ini (the runtime then leaves saves alone). */
    private boolean bonusOff() {
        File ini = new File(home(this), "mhgurecomp.ini");
        if (!ini.isFile()) return false;
        byte[] b = new byte[(int) Math.min(ini.length(), 1 << 20)];
        try (FileInputStream in = new FileInputStream(ini)) {
            int got = 0;
            for (int r; got < b.length && (r = in.read(b, got, b.length - got)) > 0; ) got += r;
        } catch (IOException e) {
            return false;
        }
        for (String line : new String(b).split("\n")) {
            String t = line.trim().toLowerCase(Locale.ROOT).replace(" ", "");
            if (t.startsWith("bonuscontent=")) return t.endsWith("=off") || t.endsWith("=0") || t.endsWith("=false");
        }
        return false;
    }

    private void confirm() {
        try {
            File save = SaveFolder.active(this);
            if (SaveFolder.isSave(new File(save, "system"))) SaveFolder.backup(this, save, "dlc");
            SaveFolder.copyChecked(found, donor(this));
        } catch (IOException e) {
            Log.e(TAG, "dlc import: " + e);
            say(es ? "No se pudo copiar. Copia la partida a la memoria del teléfono y vuelve a intentarlo."
                   : "It could not be copied. Copy the save to the phone's storage and try again.", true);
            found = null;
            main.setText(es ? "Elegir carpeta" : "Choose a folder");
            return;
        }
        Log.i(TAG, "dlc import: the downloads of " + found + " are the bonus content now (" + donor(this) + ")");
        main.setEnabled(false);
        leave.setEnabled(false);
        say(es ? "Reiniciando..." : "Restarting...", false);
        // (the game has the save in memory: it starts again so the runtime fills it before the game reads it)
        Intent intent = new Intent(this, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        startActivity(intent);
        new Handler(Looper.getMainLooper()).postDelayed(() -> Process.killProcess(Process.myPid()), 200);
    }
}
