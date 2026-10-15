// SPDX-License-Identifier: GPL-2.0-only
package org.habumi.probe.startup;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.database.Cursor;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public final class StartupSmokeActivity extends Activity {
    private static final String TAG = "HABUMI_SMOKE";
    private final String runId = UUID.randomUUID().toString();
    private TextView statusView;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        statusView = new TextView(this);
        statusView.setText("Running startup smoke checks…");
        statusView.setTextSize(18.0f);
        statusView.setPadding(24, 24, 24, 24);
        setContentView(statusView);
        new Thread(this::runChecks, "habumi-startup-smoke").start();
    }

    private void runChecks() {
        logPhase("BEGIN", "startup smoke run started");
        List<String> completed = new ArrayList<>();
        List<String> failures = new ArrayList<>();
        Uri ownedItem = null;
        boolean cleanupVerified = false;
        String itemName = "habumi-smoke-" + UUID.randomUUID() + ".txt";
        byte[] payload = ("habumi-startup-smoke:" + UUID.randomUUID())
                .getBytes(StandardCharsets.UTF_8);

        try {
            try {
                MediaCodecList codecList = new MediaCodecList(MediaCodecList.ALL_CODECS);
                int decoderCount = 0;
                boolean avcDecoderFound = false;
                for (MediaCodecInfo info : codecList.getCodecInfos()) {
                    if (!info.isEncoder()) {
                        decoderCount++;
                        for (String type : info.getSupportedTypes()) {
                            if (MediaFormat.MIMETYPE_VIDEO_AVC.equalsIgnoreCase(type)) {
                                avcDecoderFound = true;
                            }
                        }
                    }
                }
                if (decoderCount == 0 || !avcDecoderFound) {
                    throw new IllegalStateException("No AVC decoder was enumerated.");
                }
                completed.add("codec enumeration: " + decoderCount + " decoders; AVC available");
                logPhase("PASS", completed.get(completed.size() - 1));
            } catch (Exception e) {
                fail(failures, "codec enumeration", e);
            }

            MediaCodec decoder = null;
            boolean decoderStarted = false;
            try {
                decoder = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
                completed.add("AVC decoder create");
                logPhase("PASS", completed.get(completed.size() - 1));
                MediaFormat format = MediaFormat.createVideoFormat(
                        MediaFormat.MIMETYPE_VIDEO_AVC, 16, 16);
                decoder.configure(format, null, null, 0);
                completed.add("AVC decoder configure");
                logPhase("PASS", completed.get(completed.size() - 1));
                decoder.start();
                decoderStarted = true;
                completed.add("AVC decoder start");
                logPhase("PASS", completed.get(completed.size() - 1));
                decoder.stop();
                decoderStarted = false;
                completed.add("AVC decoder stop");
                logPhase("PASS", completed.get(completed.size() - 1));
            } catch (Exception e) {
                fail(failures, "AVC decoder lifecycle", e);
            } finally {
                if (decoder != null) {
                    if (decoderStarted) {
                        try {
                            decoder.stop();
                            logPhase("CLEANUP", "AVC decoder stopped after an earlier failure");
                        } catch (Exception e) {
                            fail(failures, "AVC decoder cleanup stop", e);
                        }
                    }
                    try {
                        decoder.release();
                    } catch (Exception e) {
                        fail(failures, "AVC decoder release", e);
                    }
                }
            }

            ContentResolver resolver = getContentResolver();
            try {
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.DISPLAY_NAME, itemName);
                values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                values.put(MediaStore.MediaColumns.RELATIVE_PATH,
                        Environment.DIRECTORY_DOWNLOADS + "/HabumiStartupSmoke");
                ownedItem = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                if (ownedItem == null) {
                    throw new IllegalStateException("MediaStore insert returned no URI.");
                }
                completed.add("MediaStore insert");
                logPhase("PASS", completed.get(completed.size() - 1));
                try (OutputStream output = resolver.openOutputStream(ownedItem, "w")) {
                    if (output == null) {
                        throw new IllegalStateException("MediaStore output stream was unavailable.");
                    }
                    output.write(payload);
                    output.flush();
                }
                completed.add("MediaStore write");
                logPhase("PASS", completed.get(completed.size() - 1));
                byte[] actual;
                try (InputStream input = resolver.openInputStream(ownedItem)) {
                    if (input == null) {
                        throw new IllegalStateException("MediaStore input stream was unavailable.");
                    }
                    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                    byte[] buffer = new byte[256];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        bytes.write(buffer, 0, count);
                    }
                    actual = bytes.toByteArray();
                }
                if (!java.util.Arrays.equals(payload, actual)) {
                    throw new IllegalStateException("MediaStore read-back did not match the payload.");
                }
                completed.add("MediaStore read-back verified");
                logPhase("PASS", completed.get(completed.size() - 1));
            } catch (Exception e) {
                fail(failures, "MediaStore insert/write/read", e);
            } finally {
                if (ownedItem != null) {
                    try {
                        int deleted = resolver.delete(ownedItem, null, null);
                        if (deleted != 1) {
                            throw new IllegalStateException("Delete affected " + deleted + " rows.");
                        }
                        try (Cursor cursor = resolver.query(ownedItem,
                                new String[]{MediaStore.MediaColumns._ID}, null, null, null)) {
                            if (cursor == null || cursor.getCount() != 0) {
                                throw new IllegalStateException("Deleted item is still queryable.");
                            }
                        }
                        cleanupVerified = true;
                        completed.add("MediaStore owned-item delete verified");
                        logPhase("PASS", completed.get(completed.size() - 1));
                    } catch (Exception e) {
                        fail(failures, "MediaStore owned-item cleanup", e);
                    }
                } else {
                    cleanupVerified = true;
                    completed.add("MediaStore cleanup verified: no item was inserted");
                    logPhase("PASS", completed.get(completed.size() - 1));
                }
            }
        } catch (Throwable e) {
            fail(failures, "unexpected worker failure", e);
        }

        boolean passed = failures.isEmpty() && completed.size() >= 8 && cleanupVerified;
        String details = (passed ? "PASS" : "FAIL")
                + "\nCompleted: " + String.join("; ", completed)
                + "\nFailures: " + (failures.isEmpty() ? "none" : String.join("; ", failures))
                + "\nAVC lifecycle only; no encoded frames were decoded.";
        String result = "Run: " + runId + "\n" + details;
        Log.i(TAG, "RESULT run=" + runId + " " + (passed ? "PASS" : "FAIL")
                + " " + details.replace('\n', ' '));
        runOnUiThread(() -> statusView.setText(result));
    }

    private void fail(List<String> failures, String phase, Throwable error) {
        String message = phase + ": " + error.getClass().getSimpleName()
                + (error.getMessage() == null ? "" : " - " + error.getMessage());
        failures.add(message);
        logPhase("FAIL", message);
    }

    private void logPhase(String outcome, String detail) {
        Log.i(TAG, outcome + " run=" + runId + " " + detail);
    }
}
