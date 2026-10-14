param([string]$RawRoot = 'data/raw/shakespeare')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([IO.Path]::IsPathRooted($RawRoot)) { $source = [IO.Path]::GetFullPath($RawRoot) }
else { $source = [IO.Path]::GetFullPath((Join-Path $repo $RawRoot)) }
$target = Join-Path $repo 'data/raw/shakespeare'
$utf8 = New-Object Text.UTF8Encoding($false)
$latin1 = [Text.Encoding]::GetEncoding(28591)
$linkPattern = '(?is)<a\b[^>]*\bhref\s*=\s*["'']([^"'']+)["''][^>]*>(.*?)</a>'
$seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$docs = New-Object 'Collections.Generic.List[object]'
$indexes = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$inventory = New-Object 'Collections.Generic.List[string]'
$inventory.Add("kind`twork_title`tdocument_count")
function Read-Html([string]$relative) {
    $file = Join-Path $source $relative
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing source page: $relative" }
    return [IO.File]::ReadAllText($file,$latin1)
}
function Plain-Title([string]$value) {
    return ([Net.WebUtility]::HtmlDecode(($value -replace '<[^>]*>',' ')) -replace '\s+',' ').Trim()
}
function Add-Document([string]$relative,[string]$title,[string]$mode='blockquote',[string]$marker='') {
    if (-not $seen.Add($relative)) { return }
    $null = Read-Html $relative
    $docs.Add([pscustomobject]@{Path=$relative;Title=$title;Mode=$mode;Marker=$marker})
}
$homepageHtml = Read-Html 'index.html'
$null = $indexes.Add('index.html')
$plays=0; $sonnets=0; $otherPoems=0
foreach ($link in [regex]::Matches($homepageHtml,$linkPattern)) {
    $href=$link.Groups[1].Value; $title=Plain-Title $link.Groups[2].Value
    if ($href -match '^([^/]+)/index\.html$') {
        $directory=$Matches[1]; $before=$docs.Count; $plays++
        $null=$indexes.Add($href)
        foreach ($scene in [regex]::Matches((Read-Html $href),$linkPattern)) {
            $page=$scene.Groups[1].Value
            if ($page -notmatch '^[^/]+\.html$' -or $page -in @('index.html','full.html')) { continue }
            Add-Document ($directory+'/'+$page) ($title+' | '+$page)
        }
        if ($docs.Count -eq $before) { throw "No scene documents in $href" }
        $inventory.Add("play`t$title`t$($docs.Count-$before)")
    } elseif ($href -eq 'Poetry/sonnets.html') {
        $null=$indexes.Add($href)
        foreach ($sonnet in [regex]::Matches((Read-Html $href),$linkPattern)) {
            $page=$sonnet.Groups[1].Value
            if ($page -notmatch '^sonnet\.[IVXLCDM]+\.html$') { continue }
            Add-Document ('Poetry/'+$page) ('The Sonnets | '+$page)
            $sonnets++
        }
        $inventory.Add("sonnets`tThe Sonnets`t$sonnets")
    } elseif ($href -match '^Poetry/[^/]+\.html$') {
        $mode='blockquote'; $marker=''
        if ($href -eq 'Poetry/elegy.html') { $mode='table' }
        if ($href -eq 'Poetry/VenusAndAdonis.html') { $marker='EVEN as the sun' }
        Add-Document $href $title $mode $marker
        $otherPoems++
        $inventory.Add("poem`t$title`t1")
    }
}
if ($plays -ne 37 -or $sonnets -ne 154 -or $otherPoems -ne 4) { throw "Unexpected pinned-site coverage: plays=$plays sonnets=$sonnets poems=$otherPoems" }
[void][IO.Directory]::CreateDirectory($target)
[void][IO.Directory]::CreateDirectory((Join-Path $repo 'config'))
[void][IO.Directory]::CreateDirectory((Join-Path $repo 'results/preprocess'))
$byPath=@{}; $paths=New-Object 'Collections.Generic.List[string]'
foreach ($doc in $docs) { $byPath[$doc.Path]=$doc; $paths.Add($doc.Path) }
$paths.Sort([StringComparer]::Ordinal)
$rows=New-Object 'Collections.Generic.List[string]'
$rows.Add("doc_id`twork_title`tsource_url`traw_path`tclean_path`textraction`tstart_marker")
$hashes=New-Object 'Collections.Generic.List[string]'
$hashes.Add("path`tbytes`tsha256")
$allSourcePaths=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($path in $indexes) { $null=$allSourcePaths.Add($path) }
$id=0
foreach ($path in $paths) {
    $id++; $doc=$byPath[$path]; $null=$allSourcePaths.Add($path)
    $cleanPath='data/clean/{0:D4}.txt' -f $id
    $rawPath='data/raw/shakespeare/'+$path
    $rows.Add("$id`t$($doc.Title)`thttps://shakespeare.mit.edu/$path`t$rawPath`t$cleanPath`t$($doc.Mode)`t$($doc.Marker)")
}
foreach ($path in ($allSourcePaths | Sort-Object)) {
    $from=Join-Path $source $path; $to=Join-Path $target $path
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
    if ([IO.Path]::GetFullPath($from) -ne [IO.Path]::GetFullPath($to)) { Copy-Item -LiteralPath $from -Destination $to -Force }
    $item=Get-Item -LiteralPath $to
    $sha=(Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashes.Add("$path`t$($item.Length)`t$sha")
}
[IO.File]::WriteAllLines((Join-Path $repo 'config/sources.tsv'),$rows,$utf8)
[IO.File]::WriteAllLines((Join-Path $repo 'results/preprocess/raw_checksums.tsv'),$hashes,$utf8)
[IO.File]::WriteAllLines((Join-Path $repo 'results/preprocess/corpus_inventory.tsv'),$inventory,$utf8)
$provenance=[ordered]@{website='https://shakespeare.mit.edu/';repository='https://github.com/TheMITTech/shakespeare';commit='6b82db852c7322dd33e95db347f0ddfc812c6409';original_download_date='2026-10-09';prepared_date='2026-10-14';timezone='Asia/Shanghai';play_count=$plays;sonnet_count=$sonnets;other_poem_count=$otherPoems;document_count=$docs.Count;archived_html_count=$allSourcePaths.Count;selection='Homepage works -> scene-list links, individual sonnets, four other poems. No full-play pages or legacy duplicate directories.';cleaning='Blockquoted dialogue and stage directions; speaker labels excluded. Verse-only blocks for poems, table body for Elegy; Venus starts at EVEN as the sun.'}
[IO.File]::WriteAllText((Join-Path $repo 'results/preprocess/source_snapshot.json'),($provenance|ConvertTo-Json),$utf8)
Write-Output "Selected $($docs.Count) documents from $plays plays, $sonnets sonnets and $otherPoems other poems."
