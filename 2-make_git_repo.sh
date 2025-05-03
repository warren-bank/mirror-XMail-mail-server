#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

data_dir="${DIR}/data/"
github_repo="${DIR}/repo/"

if [ ! -d "$data_dir" ];then
  echo 'ERROR: data directory for file downloads does not exist'
  exit 1
fi

[ -d "$github_repo" ] || mkdir "$github_repo"
cd "$github_repo"

[ -d '.git' ] || git init >/dev/nul 2>&1

unzip_file() {
  fname="$1"
  tgzfile="${data_dir}${fname}"

  if [ ! -s "$tgzfile" ];then
    return 1
  fi

  tar -xzf "$tgzfile" --strip-components 1 --directory .

  # add .gitkeep file to all empty directories
  find . -type d -empty -not -path './.git/*' -print -exec touch {}/.gitkeep \; >/dev/nul 2>&1
  return 0
}

while IFS= read -r line; do
  line_arr=($line)

  date=${line_arr[0]}
  version=${line_arr[1]}
  fname=${line_arr[2]}

  echo "date:    '${date}'"
  echo "version: '${version}'"
  echo "fname:   '${fname}'"
  echo ''

  # cleanup repo working tree
  git rm -rf . >/dev/nul 2>&1

  # unzip release into repo working tree
  unzip_file "$fname"
  if [ $? -ne 0 ];then
    echo "${version} zip file not found. Skipping.."
    echo ''
    continue
  fi

  echo "${version} unzipped into working tree."

  git add --all . >/dev/nul 2>&1
  git commit -m "[${date}] release for version ${version}" >/dev/nul 2>&1
  git tag "$version"
  echo "${version} committed from working tree to index"
  echo "${version} tagged"
  echo ''
  
done < "${DIR}/data.txt"

echo 'Done!'
echo 'Please remember to add remotes to the new git repo, and push.'
echo ''
