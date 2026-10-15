// SPDX-License-Identifier: GPL-2.0-only
// Adattatore minimo per le API pubbliche del jar apktool 3.0.3 verificato.
import brut.androlib.smali.SmaliBuilder;
import brut.androlib.smali.SmaliDecoder;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.writer.builder.DexBuilder;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Stream;

public final class AssembleSystemUiDex {
    public static void main(String[] args) throws Exception {
        if (args.length == 4 && args[0].equals("decode")) {
            File output = new File(args[3]);
            if (output.exists()) throw new IllegalArgumentException("Output already exists");
            new SmaliDecoder(new File(args[1]), true).decodeFile(
                new DexBackedDexFile(Files.readAllBytes(Path.of(args[2])), 0), output);
        } else if (args.length == 3 && args[0].equals("assemble")) {
            Path output = Path.of(args[2]);
            if (Files.exists(output)) throw new IllegalArgumentException("Output already exists");
            Opcodes opcodes = new Opcodes(33);
            if (opcodes.api != 33) throw new IllegalStateException("Unexpected API mapping");
            DexBuilder dex = new DexBuilder(opcodes);
            SmaliBuilder smali = new SmaliBuilder(33);
            List<Path> files;
            try (Stream<Path> paths = Files.walk(Path.of(args[1]))) {
                files = paths.filter(p -> p.toString().endsWith(".smali")).sorted().toList();
            }
            if (files.isEmpty()) throw new IllegalArgumentException("No primary smali files");
            for (Path file : files) {
                if (!smali.buildFile(file.toFile(), dex)) {
                    throw new IllegalStateException("Smali assembly failed: " + file);
                }
            }
            FileDataStore data = new FileDataStore(output.toFile());
            try { dex.writeTo(data); } finally { data.raf.close(); }
            System.out.println("Assembled primary DEX classes: " + files.size());
        } else {
            throw new IllegalArgumentException("assemble SMALI NEW_DEX | decode APK DEX NEW_DIR");
        }
    }
}
