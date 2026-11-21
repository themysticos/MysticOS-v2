cd "/mnt/c/lol/MysticOS 2"
sed -n '486,510p' fat.c
echo '=== fat_find (find) impl name compare ==='
grep -n 'int fat_find\|fat_find(' fat.c
