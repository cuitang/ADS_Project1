# Independent .NET regular-expression oracle for C counting/filtering.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$words=New-Object 'Collections.Generic.Dictionary[string,long[]]' ([StringComparer]::Ordinal)
$stops=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach($word in [IO.File]::ReadAllLines((Join-Path $repo 'config/stopwords.txt'))) { $null=$stops.Add($word) }
$regex=New-Object Text.RegularExpressions.Regex('[A-Za-z]+',[Text.RegularExpressions.RegexOptions]::CultureInvariant)
$docs=@(Import-Csv -LiteralPath (Join-Path $repo 'data/documents.tsv') -Delimiter ([char]9))
$stats=@(Import-Csv -LiteralPath (Join-Path $repo 'results/preprocess/document_stats.tsv') -Delimiter ([char]9))
if($docs.Count -ne $stats.Count) { throw 'Document statistics have the wrong row count.' }
$sources=@(Import-Csv -LiteralPath (Join-Path $repo 'config/sources.tsv') -Delimiter ([char]9))
if($docs.Count -ne $sources.Count) { throw 'Source manifest and document manifest differ.' }
$seenPaths=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$bodyHashes=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$originalTotal=[long]0; $indexedTotal=[long]0; $docIndex=0
foreach($doc in $docs) {
    $docIndex++
    if([long]$doc.doc_id -ne $docIndex -or -not $seenPaths.Add($doc.clean_path)) { throw 'Duplicate or unordered document mapping.' }
    $path=Join-Path $repo $doc.clean_path
    $text=[IO.File]::ReadAllText($path)
    if([regex]::IsMatch($text,'[^\x00-\x7f]')) { throw 'Clean text violates the documented ASCII export policy.' }
    if($text.Contains('Shakespeare homepage')) { throw 'Navigation leaked into clean text.' }
    $sha=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if(-not $bodyHashes.Add($sha)) { throw "Duplicate clean body at $($doc.clean_path)" }
    $matches=$regex.Matches($text); $seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $indexed=[long]0
    foreach($match in $matches) {
        $word=$match.Value.ToLowerInvariant()
        if(-not $words.ContainsKey($word)) { $words.Add($word,[long[]]@(0,0)) }
        $words[$word][0]++
        if($seen.Add($word)) { $words[$word][1]++ }
        if(-not $stops.Contains($word)) { $indexed++ }
    }
    $row=$stats[$docIndex-1]
    if([long]$row.doc_id -ne $docIndex -or [long]$row.original_token_count -ne $matches.Count -or [long]$row.indexed_token_count -ne $indexed) { throw "C document statistics disagree with independent regex at doc $docIndex" }
    $originalTotal+=$matches.Count; $indexedTotal+=$indexed
}
$rows=@(Import-Csv -LiteralPath (Join-Path $repo 'results/preprocess/word_counts.tsv') -Delimiter ([char]9))
if($rows.Count -ne $words.Count) { throw 'C vocabulary size disagrees with independent regex.' }
foreach($row in $rows) {
    if(-not $words.ContainsKey($row.word) -or $words[$row.word][0] -ne [long]$row.corpus_tf -or $words[$row.word][1] -ne [long]$row.document_df) { throw "C TF/DF mismatch: $($row.word)" }
    if([Math]::Abs([double]$row.document_ratio-[double]$row.document_df/$docs.Count) -gt 1e-10) { throw 'DF ratio mismatch.' }
    if([Math]::Abs([double]$row.token_ratio-[double]$row.corpus_tf/$originalTotal) -gt 1e-10) { throw 'TF ratio mismatch.' }
}
foreach($row in (Import-Csv -LiteralPath (Join-Path $repo 'results/preprocess/raw_checksums.tsv') -Delimiter ([char]9))) {
    $actual=(Get-FileHash -LiteralPath (Join-Path $repo ('data/raw/shakespeare/'+$row.path)) -Algorithm SHA256).Hash
    if($actual.ToLowerInvariant() -ne $row.sha256) { throw "Raw source hash mismatch: $($row.path)" }
}
$result=[ordered]@{status='PASS';method='Independent .NET regex [A-Za-z]+ + lowercase; compare every surface TF and DF, per-document original/indexed counts, all source SHA256 values and duplicate body hashes';documents=$docs.Count;tokens_before_filter=$originalTotal;distinct_surface_words=$words.Count;indexed_positions=$indexedTotal;clean_bodies_unique=$true;raw_checksums_verified=$true;date='2026-10-14'}
[IO.File]::WriteAllText((Join-Path $repo 'results/preprocess/independent_verification.json'),($result|ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
Write-Output ($result|ConvertTo-Json)
