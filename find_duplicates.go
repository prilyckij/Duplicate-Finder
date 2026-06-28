// find_duplicates.go
package main

import (
	"crypto/md5"
	"crypto/sha1"
	"flag"
	"fmt"
	"hash"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

const (
	reset  = "\033[0m"
	green  = "\033[92m"
	red    = "\033[91m"
	yellow = "\033[93m"
	blue   = "\033[94m"
)

func colorize(text, color string) string {
	return color + text + reset
}

type FileInfo struct {
	Path string
	Size int64
	Hash string
}

func getFileHash(path string, algo string, partial int64) (string, error) {
	var h hash.Hash
	switch algo {
	case "md5":
		h = md5.New()
	case "sha1":
		h = sha1.New()
	default:
		return "", fmt.Errorf("unsupported algorithm")
	}
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	if partial > 0 {
		// читаем только partial байт
		buf := make([]byte, partial)
		n, err := f.Read(buf)
		if err != nil && err != io.EOF {
			return "", err
		}
		h.Write(buf[:n])
	} else {
		_, err = io.Copy(h, f)
		if err != nil {
			return "", err
		}
	}
	return fmt.Sprintf("%x", h.Sum(nil)), nil
}

func findDuplicates(root string, recursive bool, algo string, partial int64, excludePatterns []string) (map[string][][]FileInfo, error) {
	sizeMap := make(map[int64][]string)
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil // пропускаем ошибки доступа
		}
		if info.IsDir() {
			if !recursive && path != root {
				return filepath.SkipDir
			}
			// Исключение папок
			for _, pat := range excludePatterns {
				matched, _ := filepath.Match(pat, info.Name())
				if matched {
					return filepath.SkipDir
				}
			}
			return nil
		}
		// Исключение файлов
		for _, pat := range excludePatterns {
			matched, _ := filepath.Match(pat, info.Name())
			if matched {
				return nil
			}
		}
		if info.Size() > 0 {
			sizeMap[info.Size()] = append(sizeMap[info.Size()], path)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}

	duplicates := make(map[string][][]FileInfo)
	for size, files := range sizeMap {
		if len(files) < 2 {
			continue
		}
		hashMap := make(map[string][]string)
		for _, f := range files {
			h, err := getFileHash(f, algo, partial)
			if err != nil {
				continue
			}
			hashMap[h] = append(hashMap[h], f)
		}
		for h, group := range hashMap {
			if len(group) > 1 {
				var infos []FileInfo
				for _, p := range group {
					info, _ := os.Stat(p)
					infos = append(infos, FileInfo{Path: p, Size: size, Hash: h})
				}
				duplicates[h] = append(duplicates[h], infos)
			}
		}
	}
	return duplicates, nil
}

func formatSize(size int64) string {
	units := []string{"B", "KB", "MB", "GB", "TB"}
	s := float64(size)
	for _, u := range units {
		if s < 1024.0 {
			return fmt.Sprintf("%.1f %s", s, u)
		}
		s /= 1024.0
	}
	return fmt.Sprintf("%.1f PB", s)
}

func printDuplicates(duplicates map[string][][]FileInfo, verbose bool) {
	if len(duplicates) == 0 {
		fmt.Println(colorize("Дубликатов не найдено.", green))
		return
	}
	var totalGroups, totalFiles int
	var totalSize int64
	for _, groups := range duplicates {
		for _, group := range groups {
			totalGroups++
			totalFiles += len(group)
			size := group[0].Size
			totalSize += size * int64(len(group)-1)
			fmt.Println(colorize(fmt.Sprintf("\nГруппа дубликатов (размер: %s):", formatSize(size)), blue))
			fmt.Printf("  Хеш: %s\n", group[0].Hash)
			for i, fi := range group {
				if i == 0 {
					fmt.Println(colorize(fmt.Sprintf("  [оригинал] %s", fi.Path), green))
				} else {
					fmt.Println(colorize(fmt.Sprintf("  [дубликат] %s", fi.Path), yellow))
				}
			}
			if verbose {
				fmt.Printf("  Количество файлов: %d\n", len(group))
			}
		}
	}
	fmt.Println(colorize(fmt.Sprintf("\nВсего групп: %d, файлов: %d, экономия: %s", totalGroups, totalFiles, formatSize(totalSize)), green))
}

func main() {
	var (
		path    string
		recursive bool
		algo    string
		partial int64
		exclude string
		deleteDups bool
		moveTo  string
		hardlink bool
		dryRun  bool
		logFile string
		verbose bool
		yes     bool
	)
	flag.StringVar(&path, "p", ".", "Папка для сканирования")
	flag.BoolVar(&recursive, "r", true, "Рекурсивно")
	flag.StringVar(&algo, "a", "md5", "Алгоритм: md5 или sha1")
	flag.Int64Var(&partial, "partial", 0, "Хешировать только первые N байт")
	flag.StringVar(&exclude, "e", "", "Исключить по glob-шаблону (через запятую)")
	flag.BoolVar(&deleteDups, "delete", false, "Удалить дубликаты")
	flag.StringVar(&moveTo, "move", "", "Переместить дубликаты в папку")
	flag.BoolVar(&hardlink, "hardlink", false, "Заменить дубликаты жёсткими ссылками")
	flag.BoolVar(&dryRun, "n", false, "Симуляция")
	flag.StringVar(&logFile, "l", "", "Лог-файл")
	flag.BoolVar(&verbose, "v", false, "Подробно")
	flag.BoolVar(&yes, "y", false, "Не запрашивать подтверждение")
	flag.Parse()
	if flag.NArg() > 0 {
		path = flag.Arg(0)
	}

	var excludePatterns []string
	if exclude != "" {
		excludePatterns = strings.Split(exclude, ",")
	}

	root, err := filepath.Abs(path)
	if err != nil {
		fmt.Println(colorize("Ошибка: "+err.Error(), red))
		os.Exit(1)
	}
	info, err := os.Stat(root)
	if err != nil || !info.IsDir() {
		fmt.Println(colorize("Ошибка: '"+root+"' не является папкой", red))
		os.Exit(1)
	}

	dups, err := findDuplicates(root, recursive, algo, partial, excludePatterns)
	if err != nil {
		fmt.Println(colorize("Ошибка сканирования: "+err.Error(), red))
		os.Exit(1)
	}
	printDuplicates(dups, verbose)

	// Действия
	if deleteDups || moveTo != "" || hardlink {
		if !yes {
			fmt.Print(colorize("Выполнить действия над дубликатами? [y/N] ", yellow))
			var ans string
			fmt.Scanln(&ans)
			if strings.ToLower(ans) != "y" {
				fmt.Println(colorize("Операция отменена.", red))
				return
			}
		}
		for _, groups := range dups {
			for _, group := range groups {
				original := group[0]
				for _, dup := range group[1:] {
					if dryRun {
						fmt.Println(colorize(fmt.Sprintf("[DRY RUN] Будет обработан дубликат: %s", dup.Path), yellow))
						continue
					}
					if deleteDups {
						os.Remove(dup.Path)
						fmt.Println(colorize(fmt.Sprintf("Удалён: %s", dup.Path), red))
					} else if moveTo != "" {
						os.MkdirAll(moveTo, 0755)
						dest := filepath.Join(moveTo, filepath.Base(dup.Path))
						os.Rename(dup.Path, dest)
						fmt.Println(colorize(fmt.Sprintf("Перемещён: %s -> %s", dup.Path, dest), yellow))
					} else if hardlink {
						os.Remove(dup.Path)
						os.Link(original.Path, dup.Path)
						fmt.Println(colorize(fmt.Sprintf("Создана жёсткая ссылка: %s -> %s", dup.Path, original.Path), blue))
					}
				}
			}
		}
	}
}
