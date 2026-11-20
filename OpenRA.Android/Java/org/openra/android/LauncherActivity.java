package org.openra.android;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class LauncherActivity extends Activity {
    private static final int REQ_OPEN_DOCUMENT = 1001;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    private TextView txtAbiInfo;
    private TextView txtStatusBadge;
    private TextView txtContentSummary;
    private TextView txtImportStatus;
    private ProgressBar progressImport;
    private Button btnDownloadFreeware;
    private Button btnImportSaf;
    private Button btnLaunch;
    private Spinner spinnerMod;
    private Spinner spinnerScale;
    private Spinner spinnerInput;

    private static final String[] MOD_IDS = { "ra", "cnc", "d2k", "ts" };
    private static final String[] MOD_LABELS = {
        "Red Alert (ra)",
        "Tiberian Dawn (cnc)",
        "Dune 2000 (d2k)",
        "Tiberian Sun (ts)"
    };
    private static final String[] SCALE_VALUES = { "1.0", "1.25", "1.5", "0.75" };
    private static final String[] INPUT_MODES = {
        "RTS Direct Touch (1-Finger Select / 2-Finger Order)",
        "Gamepad + Touch Fallback"
    };

    private int resId(String name, String defType) {
        return getResources().getIdentifier(name, defType, getPackageName());
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(resId("activity_launcher", "layout"));

        txtAbiInfo = findViewById(resId("txtAbiInfo", "id"));
        txtStatusBadge = findViewById(resId("txtStatusBadge", "id"));
        txtContentSummary = findViewById(resId("txtContentSummary", "id"));
        txtImportStatus = findViewById(resId("txtImportStatus", "id"));
        progressImport = findViewById(resId("progressImport", "id"));
        btnDownloadFreeware = findViewById(resId("btnDownloadFreeware", "id"));
        btnImportSaf = findViewById(resId("btnImportSaf", "id"));
        btnLaunch = findViewById(resId("btnLaunch", "id"));
        spinnerMod = findViewById(resId("spinnerMod", "id"));
        spinnerScale = findViewById(resId("spinnerScale", "id"));
        spinnerInput = findViewById(resId("spinnerInput", "id"));

        txtAbiInfo.setText(NativeBridge.nativeGetAbiInfo());

        spinnerMod.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, MOD_LABELS));
        spinnerScale.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, SCALE_VALUES));
        spinnerInput.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, INPUT_MODES));

        btnDownloadFreeware.setOnClickListener(v -> downloadFreewareContent());
        btnImportSaf.setOnClickListener(v -> openSafPicker());
        btnLaunch.setOnClickListener(v -> prepareAndLaunch());

        refreshContentStatus();
    }

    private File getGameRootDir() {
        return new File(getFilesDir(), "games/openra");
    }

    private File getEngineDir() {
        return new File(getFilesDir(), "engine");
    }

    private File getSupportDir() {
        return new File(getFilesDir(), "support");
    }

    private File getSelectedContentDir() {
        String modId = MOD_IDS[spinnerMod.getSelectedItemPosition()];
        return new File(getSupportDir(), "Content/" + modId);
    }

    private void refreshContentStatus() {
        File contentDir = getSelectedContentDir();
        File[] files = contentDir.exists() ? contentDir.listFiles() : null;
        int count = files != null ? files.length : 0;
        if (count > 0) {
            txtStatusBadge.setText("READY TO PLAY");
            txtStatusBadge.setTextColor(0xFF10B981);
            txtContentSummary.setText("Verified assets present (" + count + " files) in " + contentDir.getName());
        } else {
            txtStatusBadge.setText("NEEDS GAME FILES");
            txtStatusBadge.setTextColor(0xFFF59E0B);
            txtContentSummary.setText("Import original .iso, .chd, or .zip game archive via Android SAF. Assets are verified and stored privately.");
        }
    }

    private void openSafPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_OPEN_DOCUMENT);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_OPEN_DOCUMENT && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                importFromSafUri(uri);
            }
        }
    }

    private void downloadFreewareContent() {
        final String modId = MOD_IDS[spinnerMod.getSelectedItemPosition()];
        final String mirrorListUrl;
        final File targetDir;
        if ("ra".equals(modId)) {
            mirrorListUrl = "https://www.openra.net/packages/ra-quickinstall-mirrors.txt";
            targetDir = new File(getSupportDir(), "Content/ra/v2");
        } else if ("cnc".equals(modId)) {
            mirrorListUrl = "https://www.openra.net/packages/cnc-mirrors.txt";
            targetDir = new File(getSupportDir(), "Content/cnc");
        } else if ("d2k".equals(modId)) {
            mirrorListUrl = "https://www.openra.net/packages/d2k-quickinstall-v3-mirrors.txt";
            targetDir = new File(getSupportDir(), "Content/d2k");
        } else {
            Toast.makeText(this, "Direct freeware quickinstall is available for ra, cnc, and d2k.", Toast.LENGTH_LONG).show();
            return;
        }

        btnDownloadFreeware.setEnabled(false);
        btnImportSaf.setEnabled(false);
        btnLaunch.setEnabled(false);
        progressImport.setVisibility(View.VISIBLE);
        progressImport.setProgress(0);
        txtImportStatus.setText("Fetching OpenRA mirror list...");

        executor.execute(() -> {
            try {
                String zipUrl = fetchFirstMirrorUrl(mirrorListUrl);
                if (zipUrl == null || zipUrl.isEmpty()) {
                    throw new IOException("No mirror URL returned from " + mirrorListUrl);
                }

                runOnUiThread(() -> txtImportStatus.setText("Downloading " + modId + " freeware package..."));

                HttpURLConnection conn = (HttpURLConnection) new URL(zipUrl).openConnection();
                conn.setConnectTimeout(15000);
                conn.setReadTimeout(30000);
                conn.setInstanceFollowRedirects(true);
                int totalBytes = conn.getContentLength();

                if (!targetDir.exists()) {
                    targetDir.mkdirs();
                }

                int extractedFiles = 0;
                try (InputStream rawIn = conn.getInputStream();
                     ZipInputStream zis = new ZipInputStream(rawIn)) {
                    ZipEntry entry;
                    byte[] buf = new byte[16384];
                    long downloaded = 0;
                    String canonicalRoot = targetDir.getCanonicalPath() + File.separator;

                    while ((entry = zis.getNextEntry()) != null) {
                        if (entry.isDirectory()) {
                            zis.closeEntry();
                            continue;
                        }
                        File outFile = new File(targetDir, entry.getName());
                        if (!outFile.getCanonicalPath().startsWith(canonicalRoot)) {
                            throw new IOException("Blocked unsafe ZIP path: " + entry.getName());
                        }
                        File parent = outFile.getParentFile();
                        if (parent != null && !parent.exists()) {
                            parent.mkdirs();
                        }
                        try (FileOutputStream fos = new FileOutputStream(outFile)) {
                            int n;
                            while ((n = zis.read(buf)) > 0) {
                                fos.write(buf, 0, n);
                                downloaded += n;
                            }
                        }
                        extractedFiles++;
                        final int countNow = extractedFiles;
                        final int pct = totalBytes > 0
                            ? Math.min(99, (int) ((downloaded * 100L) / totalBytes))
                            : Math.min(95, 10 + countNow * 2);
                        final String nameNow = entry.getName();
                        runOnUiThread(() -> {
                            progressImport.setProgress(pct);
                            txtImportStatus.setText("Extracting: " + nameNow);
                        });
                        zis.closeEntry();
                    }
                }

                final int finalCount = extractedFiles;
                runOnUiThread(() -> {
                    btnDownloadFreeware.setEnabled(true);
                    btnImportSaf.setEnabled(true);
                    btnLaunch.setEnabled(true);
                    progressImport.setProgress(100);
                    txtImportStatus.setText("Downloaded & ready (" + finalCount + " files).");
                    refreshContentStatus();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    btnDownloadFreeware.setEnabled(true);
                    btnImportSaf.setEnabled(true);
                    btnLaunch.setEnabled(true);
                    txtImportStatus.setText("Download failed: " + e.getMessage());
                    Toast.makeText(this, "Download failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    private static String fetchFirstMirrorUrl(String listUrl) throws IOException {
        HttpURLConnection conn = (HttpURLConnection) new URL(listUrl).openConnection();
        conn.setConnectTimeout(15000);
        conn.setReadTimeout(15000);
        try (BufferedReader br = new BufferedReader(new InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.startsWith("http://") || line.startsWith("https://")) {
                    return line;
                }
            }
        }
        return null;
    }

    private void importFromSafUri(Uri uri) {
        btnDownloadFreeware.setEnabled(false);
        btnImportSaf.setEnabled(false);
        btnLaunch.setEnabled(false);
        progressImport.setVisibility(View.VISIBLE);
        progressImport.setProgress(0);
        txtImportStatus.setText("Opening SAF stream...");

        final File destDir = getSelectedContentDir();

        executor.execute(() -> {
            try (ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r")) {
                if (pfd == null) {
                    postImportResult("ERROR|Cannot open file descriptor from SAF");
                    return;
                }
                int fd = pfd.detachFd();
                String result = NativeBridge.nativeExtractAndVerify(
                    fd,
                    destDir.getAbsolutePath(),
                    (percent, message) -> runOnUiThread(() -> {
                        progressImport.setProgress(percent);
                        txtImportStatus.setText(message);
                    })
                );
                postImportResult(result);
            } catch (Exception e) {
                postImportResult("ERROR|" + e.getMessage());
            }
        });
    }

    private void postImportResult(String result) {
        runOnUiThread(() -> {
            btnDownloadFreeware.setEnabled(true);
            btnImportSaf.setEnabled(true);
            btnLaunch.setEnabled(true);
            if (result != null && result.startsWith("OK|")) {
                String[] parts = result.split("\\|");
                String files = parts.length > 1 ? parts[1] : "?";
                String sha = parts.length > 3 ? parts[3].substring(0, Math.min(16, parts[3].length())) : "";
                txtImportStatus.setText("Imported " + files + " files (SHA-256: " + sha + "...)");
                progressImport.setProgress(100);
                refreshContentStatus();
            } else {
                String err = (result != null && result.startsWith("ERROR|"))
                    ? result.substring(6)
                    : "Unknown import error";
                txtImportStatus.setText("Import failed: " + err);
                Toast.makeText(this, err, Toast.LENGTH_LONG).show();
            }
        });
    }

    private void prepareAndLaunch() {
        btnLaunch.setEnabled(false);
        txtImportStatus.setText("Preparing OpenRA engine files...");

        final String modId = MOD_IDS[spinnerMod.getSelectedItemPosition()];
        final String scale = SCALE_VALUES[spinnerScale.getSelectedItemPosition()];
        final int inputIdx = spinnerInput.getSelectedItemPosition();

        executor.execute(() -> {
            try {
                File engineDir = getEngineDir();
                File versionMarker = new File(engineDir, "VERSION");
                if (!versionMarker.exists()) {
                    copyAssetTree(getAssets(), "engine", engineDir);
                }

                File supportDir = getSupportDir();
                if (!supportDir.exists()) {
                    supportDir.mkdirs();
                }

                File gameRoot = getGameRootDir();
                if (!gameRoot.exists()) {
                    gameRoot.mkdirs();
                }

                String configJson = "{\n" +
                    "  \"mod\": \"" + modId + "\",\n" +
                    "  \"display_scale\": \"" + scale + "\",\n" +
                    "  \"input_mode\": " + inputIdx + "\n" +
                    "}\n";
                writeTextFile(new File(gameRoot, "config.json"), configJson);

                String argsTxt = "Engine.EngineDir=" + engineDir.getAbsolutePath() + "\n" +
                    "Engine.SupportDir=" + supportDir.getAbsolutePath() + "\n" +
                    "Game.Mod=" + modId + "\n";
                writeTextFile(new File(gameRoot, "args.txt"), argsTxt);

                runOnUiThread(() -> {
                    btnLaunch.setEnabled(true);
                    Intent intent = new Intent(LauncherActivity.this, OpenRAGameActivity.class);
                    intent.putExtra("ENGINE_DIR", engineDir.getAbsolutePath());
                    intent.putExtra("SUPPORT_DIR", supportDir.getAbsolutePath());
                    intent.putExtra("MOD_ID", modId);
                    startActivity(intent);
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    btnLaunch.setEnabled(true);
                    txtImportStatus.setText("Launch prep failed: " + e.getMessage());
                    Toast.makeText(this, "Launch error: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File outDir) throws IOException {
        String[] children = assets.list(assetPath);
        if (children == null || children.length == 0) {
            File parent = outDir.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            try (InputStream in = assets.open(assetPath);
                 FileOutputStream out = new FileOutputStream(outDir)) {
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            return;
        }
        if (!outDir.exists()) {
            outDir.mkdirs();
        }
        for (String child : children) {
            copyAssetTree(assets, assetPath + "/" + child, new File(outDir, child));
        }
    }

    private static void writeTextFile(File file, String content) throws IOException {
        try (FileOutputStream fos = new FileOutputStream(file)) {
            fos.write(content.getBytes(StandardCharsets.UTF_8));
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }
}
