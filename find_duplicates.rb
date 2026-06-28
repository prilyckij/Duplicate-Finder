#!/usr/bin/env ruby
# find_duplicates.rb
# encoding: UTF-8

require 'digest'
require 'find'
require 'optparse'
require 'fileutils'

COLORS = {
  green: "\e[92m",
  red: "\e[91m",
  yellow: "\e[93m",
  blue: "\e[94m",
  reset: "\e[0m"
}

def colorize(text, color)
  "#{COLORS[color]}#{text}#{COLORS[:reset]}"
end

def file_hash(path, algo, partial)
  hash = case algo
         when 'md5' then Digest::MD5.new
         when 'sha1' then Digest::SHA1.new
         else raise "Unsupported algorithm"
         end
  File.open(path, 'rb') do |f|
    total = 0
    while chunk = f.read(8192)
      if partial > 0 && total + chunk.bytesize > partial
        chunk = chunk.byteslice(0, partial - total)
      end
      hash.update(chunk)
      total += chunk.bytesize
      break if partial > 0 && total >= partial
    end
  end
  hash.hexdigest
end

def find_duplicates(root, recursive, algo, partial, exclude_patterns)
  size_map = {}
  Find.find(root) do |path|
    next unless File.file?(path)
    # Исключения
    skip = false
    exclude_patterns.each do |pat|
      if File.fnmatch(pat, File.basename(path))
        skip = true
        break
      end
    end
    next if skip
    size = File.size(path)
    next if size == 0
    size_map[size] ||= []
    size_map[size] << path
  end

  duplicates = {}
  size_map.each do |size, files|
    next if files.size < 2
    hash_map = {}
    files.each do |f|
      h = file_hash(f, algo, partial)
      hash_map[h] ||= []
      hash_map[h] << f
    end
    hash_map.each do |h, group|
      next if group.size < 2
      duplicates[h] ||= []
      duplicates[h] << { size: size, files: group }
    end
  end
  duplicates
end

def format_size(size)
  units = ['B', 'KB', 'MB', 'GB', 'TB']
  s = size.to_f
  i = 0
  while s >= 1024 && i < units.length - 1
    s /= 1024
    i += 1
  end
  sprintf("%.1f %s", s, units[i])
end

def print_duplicates(duplicates, verbose)
  if duplicates.empty?
    puts colorize("Дубликатов не найдено.", :green)
    return
  end
  total_groups = 0
  total_files = 0
  total_size = 0
  duplicates.each do |h, groups|
    groups.each do |group|
      total_groups += 1
      total_files += group[:files].size
      total_size += group[:size] * (group[:files].size - 1)
      puts colorize("\nГруппа дубликатов (размер: #{format_size(group[:size])}):", :blue)
      puts "  Хеш: #{h}"
      group[:files].each_with_index do |f, i|
        if i == 0
          puts colorize("  [оригинал] #{f}", :green)
        else
          puts colorize("  [дубликат] #{f}", :yellow)
        end
      end
      puts "  Количество файлов: #{group[:files].size}" if verbose
    end
  end
  puts colorize("\nВсего групп: #{total_groups}, файлов: #{total_files}, экономия: #{format_size(total_size)}", :green)
end

options = {
  path: '.',
  recursive: true,
  algo: 'md5',
  partial: 0,
  exclude: [],
  delete: false,
  move: nil,
  hardlink: false,
  dry_run: false,
  log: nil,
  verbose: false,
  yes: false
}

OptionParser.new do |opts|
  opts.banner = "Usage: find_duplicates.rb [options] [path]"
  opts.on("-p", "--path DIR", "Path") { |v| options[:path] = v }
  opts.on("-r", "--recursive", "Recursive") { options[:recursive] = true }
  opts.on("-a", "--algo ALGO", "md5 or sha1") { |v| options[:algo] = v }
  opts.on("--partial N", Integer, "Hash only first N bytes") { |v| options[:partial] = v }
  opts.on("-e", "--exclude PAT", "Exclude pattern") { |v| options[:exclude] << v }
  opts.on("--delete", "Delete duplicates") { options[:delete] = true }
  opts.on("--move DIR", "Move duplicates to DIR") { |v| options[:move] = v }
  opts.on("--hardlink", "Replace with hardlinks") { options[:hardlink] = true }
  opts.on("-n", "--dry-run", "Dry run") { options[:dry_run] = true }
  opts.on("-l", "--log FILE", "Log file") { |v| options[:log] = v }
  opts.on("-v", "--verbose", "Verbose") { options[:verbose] = true }
  opts.on("-y", "--yes", "No confirm") { options[:yes] = true }
  opts.on("-h", "--help", "Help") { puts opts; exit }
end.parse!

root = File.expand_path(options[:path] || ARGV[0] || '.')
unless Dir.exist?(root)
  puts colorize("Ошибка: '#{root}' не является папкой", :red)
  exit 1
end

duplicates = find_duplicates(root, options[:recursive], options[:algo],
                              options[:partial], options[:exclude])
print_duplicates(duplicates, options[:verbose])

if options[:delete] || options[:move] || options[:hardlink]
  unless options[:yes]
    print colorize("Выполнить действия над дубликатами? [y/N] ", :yellow)
    ans = $stdin.gets.chomp
    unless ans.downcase == 'y'
      puts colorize("Операция отменена.", :red)
      exit
    end
  end
  duplicates.each do |h, groups|
    groups.each do |group|
      original = group[:files].first
      group[:files][1..-1].each do |dup|
        if options[:dry_run]
          puts colorize("[DRY RUN] Будет обработан дубликат: #{dup}", :yellow)
          next
        end
        if options[:delete]
          File.delete(dup)
          puts colorize("Удалён: #{dup}", :red)
        elsif options[:move]
          FileUtils.mkdir_p(options[:move])
          dest = File.join(options[:move], File.basename(dup))
          File.rename(dup, dest)
          puts colorize("Перемещён: #{dup} -> #{dest}", :yellow)
        elsif options[:hardlink]
          File.delete(dup)
          File.link(original, dup)
          puts colorize("Создана жёсткая ссылка: #{dup} -> #{original}", :blue)
        end
      end
    end
  end
end
