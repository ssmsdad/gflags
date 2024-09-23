#!/bin/bash

# Copyright (c) 2008, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# ---
# Author: Dave Nicponski
#
# This script is invoked by bash in response to a matching compspec.  When
# this happens, bash calls this script using the command shown in the -C
# block of the complete entry, but also appends 3 arguments.  They are:
#   - The command being used for completion
#   - The word being completed
#   - The word preceding the completion word.
#
# Here's an example of how you might use this script:
# $ complete -o bashdefault -o default -o nospace -C                         \
#   '/usr/local/bin/gflags_completions.sh --tab_completion_columns $COLUMNS' \
#   time  env  binary_name  another_binary  [...]

# completion_word_index gets the index of the (N-1)th argument for
# this command line.  completion_word gets the actual argument from
# this command line at the (N-1)th position

# 此文件用于命令补全，当用户输入命令时，bash会调用此文件来获取补全的命令
# 但是这个文件怎么读也读不懂，一个问题是completion_word为什么是倒数第二个参数，而不是倒数第一个参数
# 另一个问题是binary为什么是倒数第三个参数，而不是第零个参数

# $()与${}的区别：
# $()是执行括号内的命令并返回其输出，${}是引用变量的值

# $#代表了参数的个数，$()会执行括号内的命令并返回其输出
# 使用双括号是因为双括号支持算术运算，但是单括号只是单纯的命令替换
completion_word_index="$(($# - 1))"
# $(!var)是间接引用，用于获取变量 var 的值所指向的变量的值
# 这里是获取第completion_word_index个(也就是倒数第二个)参数的值
# 假设 ./example.sh arg1 arg2 arg3 arg4，那么completion_word即为arg3
# ./example.sh不算参数，但是可以通过$0获取，arg1是$1，arg2是$2，arg3是$3，arg4是$4
completion_word="${!completion_word_index}"

# TODO(user): Replace this once gflags_completions.cc has
# a bool parameter indicating unambiguously to hijack the process for
# completion purposes.
# -z表示字符串为空，这里是判断completion_word是否为空
if [ -z "$completion_word" ]; then
  # Until an empty value for the completion word stops being misunderstood
  # by binaries, don't actually execute the binary or the process
  # won't be hijacked!
  exit 0
# 结束if语句
fi

# binary_index gets the index of the command being completed (which bash
# places in the (N-2)nd position.  binary gets the actual command from
# this command line at that (N-2)nd position
binary_index="$(($# - 2))"
# 这里是获取第binary_index个(也就是倒数第三个)参数的值，即arg2
binary="${!binary_index}"

# For completions to be universal, we may have setup the compspec to
# trigger on 'harmless pass-through' commands, like 'time' or 'env'.
# If the command being completed is one of those two, we'll need to
# identify the actual command being executed.  To do this, we need
# the actual command line that the <TAB> was pressed on.  Bash helpfully
# places this in the $COMP_LINE variable.
if [ "$binary" == "time" ] || [ "$binary" == "env" ]; then
  # we'll assume that the first 'argument' is actually the
  # binary


  # TODO(user): This is not perfect - the 'env' command, for instance,
  #   is allowed to have options between the 'env' and 'the command to
  #   be executed'.  For example, consider:
  # $ env FOO="bar"  bin/do_something  --help<TAB>
  # In this case, we'll mistake the FOO="bar" portion as the binary.
  #   Perhaps we should continuing consuming leading words until we
  #   either run out of words, or find a word that is a valid file
  #   marked as executable.  I can't think of any reason this wouldn't
  #   work.

  # Break up the 'original command line' (not this script's command line,
  # rather the one the <TAB> was pressed on) and find the second word.

  # COMP_LINE代表当前命令行的内容，()表示将COMP_LINE按空格分割成数组
  # 假设 COMP_LINE 的值为 "./example.sh arg1 arg2 arg3 arg4"：
  # 拆分后，数组 parts 的内容为 ("./example.sh" "arg1" "arg2" "arg3" "arg4")
  parts=( ${COMP_LINE} )
  # 这里是获取数组 parts 的第二个元素，也就是参数的第一个元素，arg1
  binary=${parts[1]}
fi

# Build the command line to use for completion.  Basically it involves
# passing through all the arguments given to this script (except the 3
# that bash added), and appending a '--tab_completion_word "WORD"' to
# the arguments.
params=""
for ((i=1; i<=$(($# - 3)); ++i)); do 
  # 例如 ./example.sh arg1 arg2 arg3 arg4
  # $params 的值为 "arg1"
  params="$params \"${!i}\"";
# done 用于结束for循环
done
# $params 的值为 "arg1 --tab_completion_word arg3"
params="$params --tab_completion_word \"$completion_word\""

# TODO(user): Perhaps stash the output in a temporary file somewhere
# in /tmp, and only cat it to stdout if the command returned a success
# code, to prevent false positives

# If we think we have a reasonable command to execute, then execute it
# and hope for the best.
# type -p 用于查找命令的路径，将binary的路径赋值给candidate
candidate=$(type -p "$binary")
if [ ! -z "$candidate" ]; then
  # eval用于执行命令，2>/dev/null表示将错误输出重定向到/dev/null，即不输出错误信息
  # 执行二进制文件binary，参数为params
  eval "$candidate 2>/dev/null $params"
  # -f表示文件存在且为普通文件，-x表示文件存在且可执行
elif [ -f "$binary" ] && [ -x "$binary" ]; then
  eval "$binary 2>/dev/null $params"
fi
