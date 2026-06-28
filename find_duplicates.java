// find_duplicates.java
import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.regex.Pattern;

public class find_duplicates {
    private static final String RESET = "\u001B[0m";
    private static final String GREEN = "\u001B[92m";
    private static final String RED = "\u001B[91m";
    private static final String YELLOW = "\u001B[93m";
    private static final String BLUE = "\u001B[94m";

    private static String colorize(String text, String color) {
        return color + text + RESET;
    }

    private static String getFileHash(String path, String algo, long partial) throws Exception {
        MessageDigest md = MessageDigest.getInstance(algo.toUpperCase());
        try (FileInputStream fis = new FileInputStream(path)) {
            byte[] buffer = new byte[8192];
            long total = 0;
            int read;
            while ((read = fis.read(buffer)) != -1 && (partial == 0 || total < partial)) {
                if (partial > 0 && total + read > partial)
                    read = (int)(partial - total);
                md.update(buffer, 0, read);
                total += read;
            }
        }
        byte[] digest = md.digest();
        StringBuilder sb = new StringBuilder();
        for (byte b : digest) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    private static void walk(Path dir, boolean recursive, List<String> excludePatterns,
                             Map<Long, List<String>> sizeMap) throws IOException {
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(dir)) {
            for (Path entry : stream) {
                if (Files.isDirectory(entry)) {
                    if (!recursive && !entry.equals(dir)) continue;
                    boolean skip = false;
                    for (String pat : excludePatterns)
                        if (Pattern.matches(pat, entry.getFileName().toString())) { skip = true; break; }
                    if (skip) continue;
                    walk(entry, recursive, excludePatterns, sizeMap);
                } else {
                    boolean skip = false;
                    for (String pat : excludePatterns)
                        if (Pattern.matches(pat, entry.getFileName().toString())) { skip = true; break; }
                    if (skip) continue;
                    long size = Files.size(entry);
                    if (size > 0) {
                        if (!sizeMap.containsKey(size)) sizeMap.put(size, new ArrayList<>());
                        sizeMap.get(size).add(entry.toString());
                    }
                }
            }
        }
    }

    private static Map<String, List<List<String>>> findDuplicates(String root, boolean recursive,
            String algo, long partial, List<String> excludePatterns) throws Exception {
        Map<Long, List<String>> sizeMap = new HashMap<>();
        walk(Paths.get(root), recursive, excludePatterns, sizeMap);
        Map<String, List<List<String>>> duplicates = new HashMap<>();
        for (Map.Entry<Long, List<String>> entry : sizeMap.entrySet()) {
            if (entry.getValue().size() < 2) continue;
            Map<String, List<String>> hashMap = new HashMap<>();
            for (String f : entry.getValue()) {
                String h = getFileHash(f, algo, partial);
                if (!hashMap.containsKey(h)) hashMap.put(h, new ArrayList<>());
                hashMap.get(h).add(f);
            }
            for (Map.Entry<String, List<String>> hEntry : hashMap.entrySet()) {
                if (hEntry.getValue().size() > 1) {
                    if (!duplicates.containsKey(hEntry.getKey()))
                        duplicates.put(hEntry.getKey(), new ArrayList<>());
                    duplicates.get(hEntry.getKey()).add(hEntry.getValue());
                }
            }
        }
        return duplicates;
    }

    private static String formatSize(long size) {
        String[] units = {"B", "KB", "MB", "GB", "TB"};
        double s = size;
        int i = 0;
        while (s >= 1024 && i < units.length-1) { s /= 1024; i++; }
        return String.format("%.1f %s", s, units[i]);
    }

    private static void printDuplicates(Map<String, List<List<String>>> duplicates, boolean verbose) {
        if (duplicates.isEmpty()) {
            System.out.println(colorize("Дубликатов не найдено.", GREEN));
            return;
        }
        int totalGroups = 0, totalFiles = 0;
        long totalSize = 0;
        for (Map.Entry<String, List<List<String>>> entry : duplicates.entrySet()) {
            for (List<String> group : entry.getValue()) {
                totalGroups++;
                totalFiles += group.size();
                long size = 0;
                try { size = Files.size(Paths.get(group.get(0))); } catch (IOException ignored) {}
                totalSize += size * (group.size() - 1);
                System.out.println(colorize("\nГруппа дубликатов (размер: " + formatSize(size) + "):", BLUE));
                System.out.println("  Хеш: " + entry.getKey());
                for (int i = 0; i < group.size(); i++) {
                    if (i == 0)
                        System.out.println(colorize("  [оригинал] " + group.get(i), GREEN));
                    else
                        System.out.println(colorize("  [дубликат] " + group.get(i), YELLOW));
                }
                if (verbose) System.out.println("  Количество файлов: " + group.size());
            }
        }
        System.out.println(colorize("\nВсего групп: " + totalGroups + ", файлов: " + totalFiles +
                                    ", экономия: " + formatSize(totalSize), GREEN));
    }

    public static void main(String[] args) {
        String root = ".";
        boolean recursive = true, deleteDups = false, hardlink = false, dryRun = false, verbose = false, yes = false;
        String algo = "md5", moveTo = null, logFile = null;
        long partial = 0;
        List<String> excludePatterns = new ArrayList<>();

        for (int i = 0; i < args.length; i++) {
            String arg = args[i];
            if (arg.equals("-p") && i+1 < args.length) root = args[++i];
            else if (arg.equals("-r")) recursive = true;
            else if (arg.equals("-a") && i+1 < args.length) algo = args[++i];
            else if (arg.equals("--partial") && i+1 < args.length) partial = Long.parseLong(args[++i]);
            else if (arg.equals("-e") && i+1 < args.length) excludePatterns.addAll(Arrays.asList(args[++i].split(",")));
            else if (arg.equals("--delete")) deleteDups = true;
            else if (arg.equals("--move") && i+1 < args.length) moveTo = args[++i];
            else if (arg.equals("--hardlink")) hardlink = true;
            else if (arg.equals("-n")) dryRun = true;
            else if (arg.equals("-l") && i+1 < args.length) logFile = args[++i];
            else if (arg.equals("-v")) verbose = true;
            else if (arg.equals("-y")) yes = true;
            else if (arg.equals("-h")) { System.out.println("Help..."); return; }
        }

        try {
            if (!Files.isDirectory(Paths.get(root)))
                throw new Exception("'"+root+"' не является папкой");
            Map<String, List<List<String>>> duplicates = findDuplicates(root, recursive, algo, partial, excludePatterns);
            printDuplicates(duplicates, verbose);

            if (deleteDups || moveTo != null || hardlink) {
                if (!yes) {
                    System.out.print(colorize("Выполнить действия над дубликатами? [y/N] ", YELLOW));
                    Scanner sc = new Scanner(System.in);
                    String ans = sc.nextLine();
                    if (!ans.equalsIgnoreCase("y")) {
                        System.out.println(colorize("Операция отменена.", RED));
                        return;
                    }
                }
                for (List<List<String>> groups : duplicates.values()) {
                    for (List<String> group : groups) {
                        String original = group.get(0);
                        for (int i = 1; i < group.size(); i++) {
                            String dup = group.get(i);
                            if (dryRun) {
                                System.out.println(colorize("[DRY RUN] Будет обработан дубликат: " + dup, YELLOW));
                                continue;
                            }
                            if (deleteDups) {
                                Files.delete(Paths.get(dup));
                                System.out.println(colorize("Удалён: " + dup, RED));
                            } else if (moveTo != null) {
                                Files.createDirectories(Paths.get(moveTo));
                                Path dest = Paths.get(moveTo, Paths.get(dup).getFileName().toString());
                                Files.move(Paths.get(dup), dest);
                                System.out.println(colorize("Перемещён: " + dup + " -> " + dest, YELLOW));
                            } else if (hardlink) {
                                Files.delete(Paths.get(dup));
                                Files.createLink(Paths.get(dup), Paths.get(original));
                                System.out.println(colorize("Создана жёсткая ссылка: " + dup + " -> " + original, BLUE));
                            }
                        }
                    }
                }
            }
        } catch (Exception e) {
            System.err.println(colorize("Ошибка: " + e.getMessage(), RED));
        }
    }
}
