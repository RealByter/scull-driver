savedcmd_/home/byter/Documents/scull/scull.mod := printf '%s\n'   scull.o | awk '!x[$$0]++ { print("/home/byter/Documents/scull/"$$0) }' > /home/byter/Documents/scull/scull.mod
