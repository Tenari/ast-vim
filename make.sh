#!/bin/sh

if [ "$1" = "editor" ]; then
  echo "building editor"
  rm ./build/editor
  rm -rf ./build/editor.dSYM
  #gcc -std=c99 -g -o build/editor src/tree_editor.c ./tree-sitter/libtree-sitter.a
  gcc -std=c99 -g -o build/editor src/tree_editor.c
  if [ "$2" = "run" ]; then
    ./build/editor $3
  fi
else
  echo "not a valid build"
fi
