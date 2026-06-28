// find_duplicates.js
#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { promisify } = require('util');
const glob = require('glob'); // npm install glob
const readdir = promisify(fs.readdir);
const stat = promisify(fs.stat);

const COLORS = {
    green: '\x1b[92m',
    red: '\x1b[91m',
    yellow: '\x1b[93m',
    blue: '\x1b[94m',
    reset: '\x1b[0m'
};

function colorize(text, color) {
    return COLORS[color] + text + COLORS.reset;
}

async function getFileHash(filepath, algo, partial) {
    return new Promise((resolve, reject) => {
        const hash = crypto.createHash(algo);
        const stream = fs.createReadStream(filepath);
        let bytesRead = 0;
        stream.on('data', (chunk) => {
            if (partial > 0 && bytesRead >= partial) {
                stream.destroy();
                resolve(hash.digest('hex'));
                return;
            }
            const toRead = partial > 0 ? Math.min(chunk.length, partial - bytesRead) : chunk.length;
            hash.update(chunk.slice(0, toRead));
            bytesRead += toRead;
        });
        stream.on('end', () => resolve(hash.digest('hex')));
        stream.on('error', reject);
    });
}

async function walk(dir, recursive, excludePatterns) {
    const results = [];
    const list = await readdir(dir);
    for (const item of list) {
        const full = path.join(dir, item);
        const st = await stat(full);
        if (st.isDirectory()) {
            // Исключения
            let skip = false;
            for (const pat of excludePatterns) {
                if (glob.sync(pat, { cwd: dir, strict: false }).includes(item)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            if (recursive) {
                const sub = await walk(full, recursive, excludePatterns);
                results.push(...sub);
            }
        } else {
            // Исключение файлов
            let skip = false;
            for (const pat of excludePatterns) {
                if (glob.sync(pat, { cwd: dir, strict: false }).includes(item)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            if (st.size > 0) {
                results.push({ path: full, size: st.size });
            }
        }
    }
    return results;
}

async function findDuplicates(root, recursive, algo, partial, excludePatterns) {
    const files = await walk(root, recursive, excludePatterns);
    const sizeMap = {};
    for (const f of files) {
        if (!sizeMap[f.size]) sizeMap[f.size] = [];
        sizeMap[f.size].push(f.path);
    }
    const duplicates = {};
    for (const size in sizeMap) {
        const group = sizeMap[size];
        if (group.length < 2) continue;
        const hashMap = {};
        for (const file of group) {
            const h = await getFileHash(file, algo, partial);
            if (!hashMap[h]) hashMap[h] = [];
            hashMap[h].push(file);
        }
        for (const h in hashMap) {
            if (hashMap[h].length > 1) {
                if (!duplicates[h]) duplicates[h] = [];
                duplicates[h].push({ size: parseInt(size), files: hashMap[h] });
            }
        }
    }
    return duplicates;
}

function formatSize(size) {
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let s = size;
    for (const u of units) {
        if (s < 1024) return `${s.toFixed(1)} ${u}`;
        s /= 1024;
    }
    return `${s.toFixed(1)} PB`;
}

function printDuplicates(duplicates, verbose) {
    const entries = Object.values(duplicates).flat();
    if (entries.length === 0) {
        console.log(colorize('Дубликатов не найдено.', 'green'));
        return;
    }
    let totalGroups = 0, totalFiles = 0, totalSize = 0;
    for (const entry of entries) {
        const { size, files } = entry;
        totalGroups++;
        totalFiles += files.length;
        totalSize += size * (files.length - 1);
        console.log(colorize(`\nГруппа дубликатов (размер: ${formatSize(size)}):`, 'blue'));
        console.log(`  Хеш: ${Object.keys(duplicates).find(k => duplicates[k].includes(entry))}`);
        for (let i = 0; i < files.length; i++) {
            if (i === 0) console.log(colorize(`  [оригинал] ${files[i]}`, 'green'));
            else console.log(colorize(`  [дубликат] ${files[i]}`, 'yellow'));
        }
        if (verbose) console.log(`  Количество файлов: ${files.length}`);
    }
    console.log(colorize(`\nВсего групп: ${totalGroups}, файлов: ${totalFiles}, экономия: ${formatSize(totalSize)}`, 'green'));
}

async function main() {
    const args = require('minimist')(process.argv.slice(2), {
        string: ['p', 'a', 'e', 'move', 'l'],
        boolean: ['r', 'delete', 'hardlink', 'n', 'v', 'y'],
        alias: { p: 'path', r: 'recursive', a: 'algo', e: 'exclude', n: 'dry-run', l: 'log', v: 'verbose' },
        default: { p: '.', r: true, a: 'md5', partial: 0 }
    });
    const root = path.resolve(args.p);
    const recursive = args.r;
    const algo = args.a;
    const partial = args.partial || 0;
    const excludePatterns = args.e ? args.e.split(',').map(s => s.trim()) : [];
    const deleteDups = args.delete;
    const moveTo = args.move;
    const hardlink = args.hardlink;
    const dryRun = args.n;
    const logFile = args.l;
    const verbose = args.v;
    const yes = args.y;

    try {
        const statInfo = await stat(root);
        if (!statInfo.isDirectory()) {
            console.log(colorize(`Ошибка: '${root}' не является папкой`, 'red'));
            process.exit(1);
        }
    } catch (err) {
        console.log(colorize(`Ошибка: ${err.message}`, 'red'));
        process.exit(1);
    }

    const dups = await findDuplicates(root, recursive, algo, partial, excludePatterns);
    printDuplicates(dups, verbose);

    if (deleteDups || moveTo || hardlink) {
        if (!yes) {
            const readline = require('readline').createInterface({
                input: process.stdin,
                output: process.stdout
            });
            const answer = await new Promise(resolve => {
                readline.question(colorize('Выполнить действия над дубликатами? [y/N] ', 'yellow'), resolve);
            });
            readline.close();
            if (answer.toLowerCase() !== 'y') {
                console.log(colorize('Операция отменена.', 'red'));
                return;
            }
        }
        for (const groups of Object.values(dups)) {
            for (const group of groups) {
                const original = group.files[0];
                for (let i = 1; i < group.files.length; i++) {
                    const dup = group.files[i];
                    if (dryRun) {
                        console.log(colorize(`[DRY RUN] Будет обработан дубликат: ${dup}`, 'yellow'));
                        continue;
                    }
                    if (deleteDups) {
                        fs.unlinkSync(dup);
                        console.log(colorize(`Удалён: ${dup}`, 'red'));
                    } else if (moveTo) {
                        fs.mkdirSync(moveTo, { recursive: true });
                        const dest = path.join(moveTo, path.basename(dup));
                        fs.renameSync(dup, dest);
                        console.log(colorize(`Перемещён: ${dup} -> ${dest}`, 'yellow'));
                    } else if (hardlink) {
                        fs.unlinkSync(dup);
                        fs.linkSync(original, dup);
                        console.log(colorize(`Создана жёсткая ссылка: ${dup} -> ${original}`, 'blue'));
                    }
                }
            }
        }
    }
}

main().catch(err => {
    console.log(colorize(`Ошибка: ${err.message}`, 'red'));
    process.exit(1);
});
