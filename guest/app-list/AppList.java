// SPDX-License-Identifier: GPL-2.0-only
package com.habumi.tools;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Looper;

import java.io.PrintStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.List;

public final class AppList {
    private AppList() {}

    public static void main(String[] args) {
        try {
            if (Looper.myLooper() == null) { Looper.prepareMainLooper(); }
            Context context = systemContext();
            PackageManager manager = context.getPackageManager();
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.addCategory(Intent.CATEGORY_LAUNCHER);
            List<ResolveInfo> activities = manager.queryIntentActivities(intent, 0);
            PrintStream output = new PrintStream(System.out, true,
                    StandardCharsets.UTF_8.name());
            for (ResolveInfo result : activities) {
                if (result.activityInfo == null) { continue; }
                ComponentName component = new ComponentName(
                        result.activityInfo.packageName, result.activityInfo.name);
                CharSequence label = result.loadLabel(manager);
                String name = label == null ? component.flattenToShortString()
                        : label.toString();
                output.print(component.flattenToShortString());
                output.print('\t');
                output.println(sanitize(name));
            }
            output.flush();
            if (output.checkError()) { System.exit(1); }
        } catch (Throwable error) {
            System.err.println("AppList failed: " + error.getClass().getName());
            error.printStackTrace(System.err);
            System.exit(1);
        }
    }

    private static Context systemContext() throws Exception {
        Class<?> threadClass = Class.forName("android.app.ActivityThread");
        Method systemMain = threadClass.getDeclaredMethod("systemMain");
        systemMain.setAccessible(true);
        Object thread = systemMain.invoke(null);
        Method getSystemContext = threadClass.getDeclaredMethod("getSystemContext");
        getSystemContext.setAccessible(true);
        return (Context) getSystemContext.invoke(thread);
    }

    private static String sanitize(String text) {
        StringBuilder clean = new StringBuilder(text.length());
        for (int offset = 0; offset < text.length();) {
            int codePoint = text.codePointAt(offset);
            clean.appendCodePoint(Character.isISOControl(codePoint) ? ' ' : codePoint);
            offset += Character.charCount(codePoint);
        }
        return clean.toString();
    }
}
