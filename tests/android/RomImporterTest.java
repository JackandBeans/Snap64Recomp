package org.snap64.quest;

import java.io.*;
import java.nio.file.Files;
import java.util.*;

/** Run with the supported local ROM and a temporary directory under artifacts/quest. */
public final class RomImporterTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static final class Source extends ByteArrayInputStream {
        boolean closed;
        Source(byte[] bytes) { super(bytes); }
        @Override public void close() { closed = true; }
    }
    private static void rejects(InputStream in, long size, File dest, byte[] original) throws Exception {
        boolean failed = false;
        try { RomImporter.importRom(in, size, dest, percent -> {}); }
        catch (IOException expected) { failed = true; }
        require(failed, "invalid import must fail");
        require(Arrays.equals(Files.readAllBytes(dest.toPath()), original), "failed import changed existing ROM");
        require(!new File(dest.getParent(), RomImporter.ROM_NAME + ".importing").exists(), "partial import left behind");
    }
    public static void main(String[] args) throws Exception {
        byte[] rom = Files.readAllBytes(new File(args[0]).toPath());
        File directory = new File(args[1]); Files.createDirectories(directory.toPath());
        File dest = new File(directory, RomImporter.ROM_NAME);
        List<Integer> progress = new ArrayList<>();
        Source input = new Source(rom);
        RomImporter.importRom(input, -1, dest, progress::add);
        require(input.closed, "source was not closed");
        RomImporter.validate(dest);
        require(Arrays.equals(rom, Files.readAllBytes(dest.toPath())), "import changed ROM bytes");
        require(progress.get(0) == 0 && progress.get(progress.size()-1) == 96, "missing copy/validation progress");
        for (int i=1;i<progress.size();i++) require(progress.get(i)>=progress.get(i-1), "progress moved backwards");
        System.out.println("PASS valid ROM, unknown provider size, progress, stream ownership");
        Source wrongSize = new Source(new byte[8]);
        rejects(wrongSize, 8, dest, rom); require(wrongSize.closed, "wrong-size source leaked");
        rejects(new Source(Arrays.copyOf(rom, rom.length-1)), -1, dest, rom);
        rejects(new Source(Arrays.copyOf(rom, rom.length+1)), -1, dest, rom);
        byte[] corrupt = rom.clone(); corrupt[corrupt.length-1] ^= 1;
        rejects(new Source(corrupt), rom.length, dest, rom);
        byte[] swapped = rom.clone(); for(int i=0;i<swapped.length;i+=2) { byte b=swapped[i];swapped[i]=swapped[i+1];swapped[i+1]=b; }
        rejects(new Source(swapped), swapped.length, dest, rom);
        System.out.println("PASS wrong size, truncation, overflow, corruption, wrong byte order; existing ROM preserved");
        rejects(new FilterInputStream(new Source(rom)) {
            int reads;
            @Override public int read(byte[] bytes, int offset, int count) throws IOException {
                if (++reads > 2) throw new IOException("provider disconnected");
                return super.read(bytes, offset, count);
            }
        }, -1, dest, rom);
        Thread.currentThread().interrupt();
        try { rejects(new Source(rom), -1, dest, rom); }
        finally { Thread.interrupted(); }
        System.out.println("PASS interrupted import and provider failure clean up without damaging existing ROM");
        Files.write(new File(directory, RomImporter.ROM_NAME + ".importing").toPath(), new byte[]{1,2,3});
        RomImporter.importRom(new Source(rom), rom.length, dest, percent -> {});
        RomImporter.validate(dest);
        System.out.println("PASS recovery from interrupted process and atomic replacement");
    }
}
