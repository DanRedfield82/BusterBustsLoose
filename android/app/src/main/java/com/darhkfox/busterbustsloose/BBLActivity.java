package com.darhkfox.busterbustsloose;

import android.content.Intent;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.libsdl.app.SDLActivity;

/**
 * The port's activity: SDL runs the game (libmain.so); this adds the ROM picker.
 * Android has no folder next to the program to drop the ROM in, so the player
 * picks it once with the system file picker and it is copied into app storage
 * (files/rom.sfc, which main.c looks for first).
 */
public class BBLActivity extends SDLActivity {
    private static final int PICK_ROM = 0x4242;
    private static final long MAX_ROM = 16L << 20;
    // set when the picker returns: the copied ROM's path, or "" if cancelled
    private static volatile String romResult;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // called from native code (main.c, androidPickRom)
    public static void pickRom() {
        romResult = null;
        final SDLActivity act = mSingleton;
        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try {
                    act.startActivityForResult(i, PICK_ROM);
                } catch (Exception e) {
                    romResult = "";
                }
            }
        });
    }

    // polled from native code until the picker has returned (null while it is open)
    public static String pollRomResult() {
        String r = romResult;
        if (r != null) romResult = null;
        return r;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_ROM) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String result = "";
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            File out = new File(getFilesDir(), "rom.sfc");
            File tmp = new File(getFilesDir(), "rom.tmp");
            boolean copied = false;
            try (InputStream in = getContentResolver().openInputStream(data.getData());
                 OutputStream os = new FileOutputStream(tmp)) {
                byte[] buf = new byte[65536];
                long total = 0;
                int n;
                while ((n = in.read(buf)) > 0) {
                    os.write(buf, 0, n);
                    total += n;
                    if (total > MAX_ROM) throw new Exception("not a SNES ROM (too big)");
                }
                copied = true;
            } catch (Exception e) {
                copied = false;
            }
            if (copied) {
                out.delete();
                if (tmp.renameTo(out)) result = out.getAbsolutePath();
            } else {
                tmp.delete();
            }
        }
        romResult = result;
    }
}
