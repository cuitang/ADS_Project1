param([string]$Compiler = '', [switch]$SkipThresholdIndexes)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$utf8=New-Object Text.UTF8Encoding($false)
if (-not $Compiler) {
    $found=Get-Command gcc -ErrorAction SilentlyContinue
    if ($found) { $Compiler=$found.Source }
    elseif (Test-Path -LiteralPath 'C:\msys64\ucrt64\bin\gcc.exe') { $Compiler='C:\msys64\ucrt64\bin\gcc.exe' }
    else { throw 'GCC not found. Pass -Compiler <path-to-gcc.exe>.' }
}
$env:Path=(Split-Path -Parent $Compiler)+';'+$env:Path
Push-Location $repo
try {
    foreach($dir in @('build','build/index_tests','results/preprocess','data/clean')) { [void][IO.Directory]::CreateDirectory((Join-Path $repo $dir)) }
    $log=New-Object 'Collections.Generic.List[string]'
    function Run-Native([string]$Exe,[string[]]$Arguments) {
        $saved=$ErrorActionPreference; $ErrorActionPreference='Continue'
        try { $output=@(& $Exe @Arguments 2>&1); $exit=$LASTEXITCODE } finally { $ErrorActionPreference=$saved }
        foreach($item in $output) { $line=$item.ToString(); Write-Output $line; $log.Add($line) }
        if($exit -ne 0) { throw "Native command failed with exit $exit : $Exe" }
    }
    $flags=@('-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I','includes')
    Run-Native $Compiler @('-std=c11','-O2','-Wall','-Wextra','-Wno-parentheses','-Wno-misleading-indentation','-c','third_party/porter/porter.c','-o','build/porter.o')
    $common=@('src/preprocess/AdsPreprocess.c','build/porter.o')
    Run-Native $Compiler ($flags+$common+@('src/preprocess/ads_prepare.c','-o','build/ads_prepare.exe'))
    Run-Native $Compiler ($flags+$common+@('tests/preprocess/test_preprocess.c','-o','build/test_preprocess.exe'))
    Run-Native $Compiler ($flags+$common+@('src/index/AdsIndex.c','src/preprocess/build_index_from_a.c','-o','build/build_index_from_a.exe'))
    Run-Native $Compiler ($flags+$common+@('src/index/AdsIndex.c','tests/preprocess/test_full_index.c','-o','build/test_full_index.exe'))
    Run-Native $Compiler ($flags+$common+@('src/index/AdsIndex.c','tests/preprocess/probe_stopwords.c','-o','build/probe_stopwords.exe'))
    Run-Native $Compiler ($flags+@('src/index/AdsIndex.c','tests/index/index_operation_test.c','-o','build/index_operation_test.exe'))
    Run-Native './build/test_preprocess.exe' @()
    Run-Native './build/index_operation_test.exe' @('build/index_tests')
    & (Join-Path $PSScriptRoot 'prepare_corpus.ps1')
    Run-Native './build/ads_prepare.exe' @('clean','config/sources.tsv','data/documents.tsv')
    Run-Native './build/ads_prepare.exe' @('count','data/documents.tsv','results/preprocess/word_counts.tsv')
    Run-Native './build/ads_prepare.exe' @('select','results/preprocess/word_counts.tsv','config/function_words.txt','config','results/preprocess')
    Run-Native './build/ads_prepare.exe' @('emit','data/documents.tsv','config/stopwords.txt','data/tokens.tsv','results/preprocess/document_stats.tsv')
    Run-Native './build/ads_prepare.exe' @('emit','data/test/documents.tsv','data/test/demo_stopwords.txt','build/sample_tokens.tsv','build/sample_doc_stats.tsv')
    $actual=[IO.File]::ReadAllText((Join-Path $repo 'build/sample_tokens.tsv')).Replace("`r`n","`n").TrimEnd()
    $expected=[IO.File]::ReadAllText((Join-Path $repo 'data/test/expected_tokens.tsv')).Replace("`r`n","`n").TrimEnd()
    if($actual -ne $expected) { throw 'C CLI sample tokens disagree with the hand-checked fixture.' }
    Run-Native './build/build_index_from_a.exe' @('build/sample_tokens.tsv','data/test/documents.tsv','build/sample_index')
    $actual=[IO.File]::ReadAllText((Join-Path $repo 'build/sample_index/postings.tsv')).Replace("`r`n","`n").TrimEnd()
    $expected=[IO.File]::ReadAllText((Join-Path $repo 'data/test/expected_index.tsv')).Replace("`r`n","`n").TrimEnd()
    if($actual -ne $expected) { throw 'A -> B sample postings disagree with the hand-checked fixture.' }
    Run-Native './build/build_index_from_a.exe' @('data/tokens.tsv','data/documents.tsv','build/index_balanced')
    Run-Native './build/test_full_index.exe' @('data/documents.tsv','config/stopwords.txt','build/index_balanced')
    $sensitivity=New-Object 'Collections.Generic.List[string]'
    $policies=@([pscustomobject]@{Name='balanced';Stop='config/stopwords.txt';Tokens='data/tokens.tsv'})
    if(-not $SkipThresholdIndexes) { $policies+=@([pscustomobject]@{Name='conservative';Stop='config/stopwords_conservative.txt';Tokens='build/tokens_conservative.tsv'},[pscustomobject]@{Name='aggressive';Stop='config/stopwords_aggressive.txt';Tokens='build/tokens_aggressive.tsv'}) }
    foreach($policy in $policies) {
        $name=$policy.Name; $stop=$policy.Stop; $tokens=$policy.Tokens; $index='build/index_'+$name
        if($name -ne 'balanced') {
            Run-Native './build/ads_prepare.exe' @('emit','data/documents.tsv',$stop,$tokens,('build/doc_stats_'+$name+'.tsv'))
            Run-Native './build/build_index_from_a.exe' @($tokens,'data/documents.tsv',$index)
        }
        $rows=@(& './build/probe_stopwords.exe' $stop $index $name)
        if($LASTEXITCODE -ne 0) { throw 'Threshold probe failed.' }
        for($i=0;$i -lt $rows.Count;$i++) { if($sensitivity.Count -eq 0 -or $i -gt 0) { $sensitivity.Add($rows[$i]) } }
    }
    [IO.File]::WriteAllLines((Join-Path $repo 'results/preprocess/query_stopword_sensitivity.tsv'),$sensitivity,$utf8)
    $configuration=[ordered]@{version='ads-a-v1';tokenizer='[A-Za-z]+; ASCII lowercase; punctuation and digits are separators';position_base=0;position_counted_before_stopword_filter=$true;stopword_filter='normalized surface BEFORE Porter stemming';stemmer='Martin Porter thread-safe ANSI C Release 4, 2014-03-25';stopword_sha256=(Get-FileHash -LiteralPath 'config/stopwords.txt' -Algorithm SHA256).Hash.ToLowerInvariant();sources_sha256=(Get-FileHash -LiteralPath 'config/sources.tsv' -Algorithm SHA256).Hash.ToLowerInvariant();tokens_sha256=(Get-FileHash -LiteralPath 'data/tokens.tsv' -Algorithm SHA256).Hash.ToLowerInvariant();b_build_stopwords_argument='NULL';phrase_rule='keep_stopwords=1, compare non-stopword stems and stopword surfaces at original positions'}
    [IO.File]::WriteAllText((Join-Path $repo 'data/preprocessing.json'),($configuration|ConvertTo-Json),$utf8)
    $compilerText=(& $Compiler --version | Select-Object -First 1)
    $report=[ordered]@{date='2026-10-14';timezone='Asia/Shanghai';compiler=$compilerText;flags='-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror (our C code)';official_porter_pairs=23531;preprocessing_unit_tests='PASS';existing_b_tests='PASS';manual_sample_cli_and_index='PASS';full_corpus_index_roundtrip='PASS';full_corpus_positions_12_query_probes='PASS';stopword_policies_tested=($policies|ForEach-Object{$_.Name});query_member_c_implementation='Not present in supplied ZIP; API handoff provided, no C end-to-end claim.'}
    [IO.File]::WriteAllText((Join-Path $repo 'results/preprocess/test_results.json'),($report|ConvertTo-Json),$utf8)
    [IO.File]::WriteAllLines((Join-Path $repo 'results/preprocess/run_A.log'),$log,$utf8)
    Write-Output 'Member A pipeline and A/B integration completed successfully.'
} finally { Pop-Location }
