package org.snap64.quest;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Bounded, transactional import. No Android dependencies so failure paths can be tested. */
public final class RomImporter {
    public static final String ROM_NAME = "pokemonsnap.z64";
    public static final long ROM_SIZE = 16L * 1024 * 1024;
    // USA Rev 1, big-endian. Same image as main.cpp's XXH3 73CBBC5C7DE9425C.
    private static final String SHA256 = "a1d5d816db7f8557ee04c35a011326d058b2c1fbca76b57b352b1d705a1ec1cc";
    public interface Progress { void update(int percent) throws IOException; }

    private static MessageDigest digest() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new AssertionError(e); }
    }
    private static void checkInterrupted() throws InterruptedIOException {
        if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException("Import cancelled.");
    }
    private static void checkHash(MessageDigest digest, long bytes) throws IOException {
        StringBuilder hex = new StringBuilder();
        for (byte b : digest.digest()) hex.append(String.format("%02x", b & 255));
        if (bytes != ROM_SIZE || !SHA256.equals(hex.toString()))
            throw new IOException("This is not the supported Pokémon Snap (USA Rev 1) .z64 ROM. Choose an unmodified US ROM in .z64 format.");
    }
    public static void validate(File rom) throws IOException {
        if (rom.length() != ROM_SIZE) throw new IOException("The ROM is incomplete or is not the supported Pokémon Snap .z64 file.");
        MessageDigest hash = digest();
        long total = 0;
        try (InputStream in = new FileInputStream(rom)) {
            byte[] buffer = new byte[65536]; int count;
            while ((count = in.read(buffer)) != -1) {
                checkInterrupted(); total += count;
                if (total > ROM_SIZE) throw new IOException("The ROM is too large.");
                hash.update(buffer, 0, count);
            }
        }
        checkHash(hash, total);
    }
    /** Owns and closes source, including when validation or copying fails. */
    public static void importRom(InputStream source, long advertisedSize, File destination, Progress progress) throws IOException {
        File partial = new File(destination.getParentFile(), ROM_NAME + ".importing");
        try (InputStream in = source) {
            if (in == null) throw new IOException("The selected file could not be opened.");
            if (advertisedSize >= 0 && advertisedSize != ROM_SIZE)
                throw new IOException("Choose the 16 MB Pokémon Snap (USA Rev 1) .z64 ROM.");
            MessageDigest hash = digest(); long total = 0; int last = -1;
            progress.update(0);
            try (FileOutputStream out = new FileOutputStream(partial)) {
                byte[] buffer = new byte[65536]; int count;
                while ((count = in.read(buffer)) != -1) {
                    checkInterrupted(); total += count;
                    if (total > ROM_SIZE) throw new IOException("The selected file is larger than a Pokémon Snap ROM.");
                    out.write(buffer, 0, count); hash.update(buffer, 0, count);
                    int percent = (int)(total * 95 / ROM_SIZE);
                    if (percent != last) { progress.update(percent); last = percent; }
                }
                progress.update(96);
                checkHash(hash, total);
                out.getFD().sync();
            }
            checkInterrupted();
            Files.move(partial.toPath(), destination.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(partial.toPath());
        }
    }
    private RomImporter() {}
}
