// find_duplicates.cs
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

class FindDuplicates
{
    static string Colorize(string text, string color)
    {
        string col = color switch
        {
            "green" => "\x1b[92m",
            "red" => "\x1b[91m",
            "yellow" => "\x1b[93m",
            "blue" => "\x1b[94m",
            _ => "\x1b[0m"
        };
        return col + text + "\x1b[0m";
    }

    static string GetFileHash(string path, string algo, long partial)
    {
        using var fs = new FileStream(path, FileMode.Open, FileAccess.Read);
        HashAlgorithm hash = algo.ToLower() switch
        {
            "md5" => MD5.Create(),
            "sha1" => SHA1.Create(),
            _ => throw new NotSupportedException()
        };
        byte[] buffer = new byte[8192];
        long total = 0;
        int read;
        while ((read = fs.Read(buffer, 0, buffer.Length)) > 0 && (partial == 0 || total < partial))
        {
            if (partial > 0 && total + read > partial)
                read = (int)(partial - total);
            hash.TransformBlock(buffer, 0, read, buffer, 0);
            total += read;
            if (partial > 0 && total >= partial) break;
        }
        hash.TransformFinalBlock(buffer, 0, 0);
        return BitConverter.ToString(hash.Hash).Replace("-", "").ToLower();
    }

    static void FindDuplicates(string root, bool recursive, string algo, long partial,
                               List<string> excludePatterns,
                               Dictionary<string, List<List<string>>> duplicates)
    {
        var sizeMap = new Dictionary<long, List<string>>();
        void Walk(string dir)
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(dir))
            {
                if (Directory.Exists(entry))
                {
                    if (!recursive && entry != root) continue;
                    bool skip = false;
                    foreach (var pat in excludePatterns)
                        if (Regex.IsMatch(Path.GetFileName(entry), WildcardToRegex(pat))) { skip = true; break; }
                    if (skip) continue;
                    Walk(entry);
                }
                else
                {
                    bool skip = false;
                    foreach (var pat in excludePatterns)
                        if (Regex.IsMatch(Path.GetFileName(entry), WildcardToRegex(pat))) { skip = true; break; }
                    if (skip) continue;
                    var info = new FileInfo(entry);
                    if (info.Length > 0)
                    {
                        if (!sizeMap.ContainsKey(info.Length))
                            sizeMap[info.Length] = new List<string>();
                        sizeMap[info.Length].Add(entry);
                    }
                }
            }
        }
        Walk(root);

        foreach (var kv in sizeMap)
        {
            if (kv.Value.Count < 2) continue;
            var hashMap = new Dictionary<string, List<string>>();
            foreach (var f in kv.Value)
            {
                var h = GetFileHash(f, algo, partial);
                if (!hashMap.ContainsKey(h))
                    hashMap[h] = new List<string>();
                hashMap[h].Add(f);
            }
            foreach (var hkv in hashMap)
            {
                if (hkv.Value.Count > 1)
                {
                    if (!duplicates.ContainsKey(hkv.Key))
                        duplicates[hkv.Key] = new List<List<string>>();
                    duplicates[hkv.Key].Add(hkv.Value);
                }
            }
        }
    }

    static string WildcardToRegex(string pattern) =>
        "^" + Regex.Escape(pattern).Replace("\\*", ".*").Replace("\\?", ".") + "$";

    static string FormatSize(long size)
    {
        string[] units = { "B", "KB", "MB", "GB", "TB" };
        double s = size;
        int i = 0;
        while (s >= 1024 && i < units.Length - 1) { s /= 1024; i++; }
        return $"{s:F1} {units[i]}";
    }

    static void PrintDuplicates(Dictionary<string, List<List<string>>> duplicates, bool verbose)
    {
        if (duplicates.Count == 0)
        {
            Console.WriteLine(Colorize("Дубликатов не найдено.", "green"));
            return;
        }
        int totalGroups = 0, totalFiles = 0;
        long totalSize = 0;
        foreach (var kv in duplicates)
        {
            foreach (var group in kv.Value)
            {
                totalGroups++;
                totalFiles += group.Count;
                var size = new FileInfo(group[0]).Length;
                totalSize += size * (group.Count - 1);
                Console.WriteLine(Colorize($"\nГруппа дубликатов (размер: {FormatSize(size)}):", "blue"));
                Console.WriteLine($"  Хеш: {kv.Key}");
                for (int i = 0; i < group.Count; i++)
                {
                    if (i == 0)
                        Console.WriteLine(Colorize($"  [оригинал] {group[i]}", "green"));
                    else
                        Console.WriteLine(Colorize($"  [дубликат] {group[i]}", "yellow"));
                }
                if (verbose) Console.WriteLine($"  Количество файлов: {group.Count}");
            }
        }
        Console.WriteLine(Colorize($"\nВсего групп: {totalGroups}, файлов: {totalFiles}, экономия: {FormatSize(totalSize)}", "green"));
    }

    static void Main(string[] args)
    {
        string root = ".";
        bool recursive = true, deleteDups = false, hardlink = false, dryRun = false, verbose = false, yes = false;
        string algo = "md5", moveTo = null, logFile = null;
        long partial = 0;
        var excludePatterns = new List<string>();

        for (int i = 0; i < args.Length; i++)
        {
            string arg = args[i];
            if (arg == "-p" && i+1 < args.Length) root = args[++i];
            else if (arg == "-r") recursive = true;
            else if (arg == "-a" && i+1 < args.Length) algo = args[++i];
            else if (arg == "--partial" && i+1 < args.Length) partial = long.Parse(args[++i]);
            else if (arg == "-e" && i+1 < args.Length) excludePatterns.AddRange(args[++i].Split(','));
            else if (arg == "--delete") deleteDups = true;
            else if (arg == "--move" && i+1 < args.Length) moveTo = args[++i];
            else if (arg == "--hardlink") hardlink = true;
            else if (arg == "-n") dryRun = true;
            else if (arg == "-l" && i+1 < args.Length) logFile = args[++i];
            else if (arg == "-v") verbose = true;
            else if (arg == "-y") yes = true;
            else if (arg == "-h") { Console.WriteLine("Help..."); return; }
        }

        if (!Directory.Exists(root))
        {
            Console.WriteLine(Colorize($"Ошибка: '{root}' не является папкой", "red"));
            return;
        }

        var duplicates = new Dictionary<string, List<List<string>>>();
        FindDuplicates(root, recursive, algo, partial, excludePatterns, duplicates);
        PrintDuplicates(duplicates, verbose);

        if (deleteDups || moveTo != null || hardlink)
        {
            if (!yes)
            {
                Console.Write(Colorize("Выполнить действия над дубликатами? [y/N] ", "yellow"));
                var ans = Console.ReadLine();
                if (ans?.ToLower() != "y")
                {
                    Console.WriteLine(Colorize("Операция отменена.", "red"));
                    return;
                }
            }
            foreach (var groups in duplicates.Values)
            {
                foreach (var group in groups)
                {
                    var original = group[0];
                    for (int i = 1; i < group.Count; i++)
                    {
                        var dup = group[i];
                        if (dryRun)
                        {
                            Console.WriteLine(Colorize($"[DRY RUN] Будет обработан дубликат: {dup}", "yellow"));
                            continue;
                        }
                        if (deleteDups)
                        {
                            File.Delete(dup);
                            Console.WriteLine(Colorize($"Удалён: {dup}", "red"));
                        }
                        else if (moveTo != null)
                        {
                            Directory.CreateDirectory(moveTo);
                            var dest = Path.Combine(moveTo, Path.GetFileName(dup));
                            File.Move(dup, dest);
                            Console.WriteLine(Colorize($"Перемещён: {dup} -> {dest}", "yellow"));
                        }
                        else if (hardlink)
                        {
                            File.Delete(dup);
                            File.CreateSymbolicLink(dup, original);
                            Console.WriteLine(Colorize($"Создана жёсткая ссылка: {dup} -> {original}", "blue"));
                        }
                    }
                }
            }
        }
    }
}
