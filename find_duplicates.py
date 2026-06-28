# find_duplicates.py
#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import argparse
import hashlib
import shutil
import fnmatch
from collections import defaultdict
from pathlib import Path

# ANSI colors
COLORS = {
    'green': '\033[92m',
    'red': '\033[91m',
    'yellow': '\033[93m',
    'blue': '\033[94m',
    'reset': '\033[0m'
}

def colorize(text, color):
    return f"{COLORS.get(color, '')}{text}{COLORS['reset']}"

def get_file_hash(filepath, algo='md5', partial=0):
    """Вычисляет хеш файла. Если partial > 0, читает только первые partial байт."""
    hash_func = hashlib.new(algo)
    try:
        with open(filepath, 'rb') as f:
            if partial > 0:
                chunk = f.read(partial)
                hash_func.update(chunk)
                # Если файл больше, всё равно хешируем только начало (быстро)
            else:
                for chunk in iter(lambda: f.read(8192), b''):
                    hash_func.update(chunk)
        return hash_func.hexdigest()
    except (IOError, OSError):
        return None

def find_duplicates(root, recursive=True, algo='md5', partial=0, exclude=None):
    """Находит дубликаты файлов в указанной папке."""
    exclude = exclude or []
    size_map = defaultdict(list)
    # Первый проход: группировка по размеру
    for dirpath, dirnames, filenames in os.walk(root):
        if not recursive and dirpath != root:
            continue
        # Исключения для папок
        skip_dir = False
        for pat in exclude:
            if fnmatch.fnmatch(os.path.basename(dirpath), pat):
                skip_dir = True
                break
        if skip_dir:
            continue
        for fname in filenames:
            # Исключения для файлов
            skip_file = False
            for pat in exclude:
                if fnmatch.fnmatch(fname, pat):
                    skip_file = True
                    break
            if skip_file:
                continue
            full = os.path.join(dirpath, fname)
            try:
                size = os.path.getsize(full)
                if size > 0:  # пропускаем пустые файлы
                    size_map[size].append(full)
            except OSError:
                continue

    # Второй проход: хеширование для групп с одинаковым размером
    duplicates = defaultdict(list)
    for size, files in size_map.items():
        if len(files) < 2:
            continue
        hash_map = defaultdict(list)
        for f in files:
            h = get_file_hash(f, algo, partial)
            if h is not None:
                hash_map[h].append(f)
        for h, group in hash_map.items():
            if len(group) > 1:
                duplicates[h].append((size, group))
    return duplicates

def format_size(size):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if size < 1024.0:
            return f"{size:.1f} {unit}"
        size /= 1024.0
    return f"{size:.1f} PB"

def print_duplicates(duplicates, verbose=False):
    if not duplicates:
        print(colorize("Дубликатов не найдено.", 'green'))
        return
    total_groups = 0
    total_files = 0
    total_size = 0
    for hash_val, groups in duplicates.items():
        for size, group in groups:
            total_groups += 1
            total_files += len(group)
            total_size += size * len(group) - size  # экономия
            print(colorize(f"\nГруппа дубликатов (размер: {format_size(size)}):", 'blue'))
            print(f"  Хеш: {hash_val}")
            for i, f in enumerate(group):
                if i == 0:
                    print(colorize(f"  [оригинал] {f}", 'green'))
                else:
                    print(colorize(f"  [дубликат] {f}", 'yellow'))
            if verbose:
                print(f"  Количество файлов: {len(group)}")
    print(colorize(f"\nВсего групп: {total_groups}, файлов: {total_files}, экономия: {format_size(total_size)}", 'green'))

def main():
    parser = argparse.ArgumentParser(description="Duplicate Finder – поиск дубликатов файлов")
    parser.add_argument('path', nargs='?', default='.', help='Папка для сканирования (по умолчанию текущая)')
    parser.add_argument('-r', '--recursive', action='store_true', default=True, help='Рекурсивно (включено)')
    parser.add_argument('-a', '--algo', choices=['md5', 'sha1'], default='md5', help='Алгоритм хеширования')
    parser.add_argument('--partial', type=int, default=0, help='Хешировать только первые N байт (для больших файлов)')
    parser.add_argument('-e', '--exclude', action='append', default=[], help='Исключить по glob-шаблону')
    parser.add_argument('--delete', action='store_true', help='Удалить дубликаты (оставить по одному)')
    parser.add_argument('--move', help='Переместить дубликаты в указанную папку')
    parser.add_argument('--hardlink', action='store_true', help='Заменить дубликаты жёсткими ссылками на оригинал')
    parser.add_argument('-n', '--dry-run', action='store_true', help='Только показать действия')
    parser.add_argument('-l', '--log', help='Сохранить отчёт в файл')
    parser.add_argument('-v', '--verbose', action='store_true', help='Подробный вывод')
    parser.add_argument('-y', '--yes', action='store_true', help='Не запрашивать подтверждение')
    args = parser.parse_args()

    root = os.path.abspath(args.path)
    if not os.path.isdir(root):
        sys.exit(colorize(f"Ошибка: '{root}' не является папкой", 'red'))

    duplicates = find_duplicates(root, args.recursive, args.algo, args.partial, args.exclude)
    if args.log:
        with open(args.log, 'w') as f:
            # Сохраняем в файл
            pass  # упрощённо
    print_duplicates(duplicates, args.verbose)

    # Действия с дубликатами (если указаны)
    if args.delete or args.move or args.hardlink:
        if not args.yes:
            answer = input(colorize("Выполнить действия над дубликатами? [y/N] ", 'yellow'))
            if answer.lower() != 'y':
                print(colorize("Операция отменена.", 'red'))
                return
        # Реализация действий (упрощённо)
        for hash_val, groups in duplicates.items():
            for size, group in groups:
                original = group[0]
                for dup in group[1:]:
                    if args.dry_run:
                        print(colorize(f"[DRY RUN] Будет обработан дубликат: {dup}", 'yellow'))
                        continue
                    if args.delete:
                        os.remove(dup)
                        print(colorize(f"Удалён: {dup}", 'red'))
                    elif args.move:
                        os.makedirs(args.move, exist_ok=True)
                        dest = os.path.join(args.move, os.path.basename(dup))
                        shutil.move(dup, dest)
                        print(colorize(f"Перемещён: {dup} -> {dest}", 'yellow'))
                    elif args.hardlink:
                        os.remove(dup)
                        os.link(original, dup)
                        print(colorize(f"Создана жёсткая ссылка: {dup} -> {original}", 'blue'))

if __name__ == '__main__':
    main()
