# Synthetic vocabulary-growth experiment. Only one document per case;
# does NOT simulate 400 million words or 500,000 independent files.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bench=Join-Path $repo 'build/scale_benchmark'
[void][IO.Directory]::CreateDirectory($bench)
$utf8=New-Object Text.UTF8Encoding($false)
$rows=New-Object 'Collections.Generic.List[string]'
$rows.Add("documents`tdistinct_words`ttokens`twall_seconds`tprocess_cpu_seconds`tobserved_peak_working_set_bytes")
foreach($size in @(1000,10000,100000)) {
    $text=New-Object Text.StringBuilder
    for($i=0;$i -lt $size;$i++) {
        $v=$i; $letters=New-Object char[] 7; $letters[0]='w'
        for($j=6;$j -ge 1;$j--) { $letters[$j]=[char](97+($v%26)); $v=[int][Math]::Floor($v/26) }
        $null=$text.Append((-join $letters)).Append(' ')
    }
    $relative='build/scale_benchmark/input_'+$size+'.txt'
    [IO.File]::WriteAllText((Join-Path $repo $relative),$text.ToString(),$utf8)
    $manifest=Join-Path $bench ('documents_'+$size+'.tsv')
    [IO.File]::WriteAllText($manifest,"doc_id`twork_title`tsource_url`tclean_path`n1`tSynthetic vocabulary growth`tbenchmark://synthetic`t$relative`n",$utf8)
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=Join-Path $repo 'build/ads_prepare.exe'
    $start.Arguments='count build/scale_benchmark/documents_'+$size+'.tsv build/scale_benchmark/counts_'+$size+'.tsv'
    $start.WorkingDirectory=$repo; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process; $process.StartInfo=$start
    $clock=[Diagnostics.Stopwatch]::StartNew(); $null=$process.Start(); $peak=[long]0
    while(-not $process.HasExited) {
        try { $process.Refresh(); $peak=[Math]::Max($peak,$process.PeakWorkingSet64) } catch { }
        Start-Sleep -Milliseconds 1
    }
    $stdout=$process.StandardOutput.ReadToEnd(); $stderr=$process.StandardError.ReadToEnd()
    $process.WaitForExit(); $clock.Stop()
    if($process.ExitCode -ne 0) { throw "Benchmark failed: $stdout $stderr" }
    $cpu=$process.TotalProcessorTime.TotalSeconds
    $actual=@(Import-Csv -LiteralPath (Join-Path $bench ('counts_'+$size+'.tsv')) -Delimiter ([char]9))
    if($actual.Count -ne $size -or ($actual|Where-Object{$_.corpus_tf -ne '1' -or $_.document_df -ne '1'})) { throw 'Synthetic benchmark correctness check failed.' }
    $rows.Add(('1'+"`t"+$size+"`t"+$size+"`t"+$clock.Elapsed.TotalSeconds.ToString('F6',[Globalization.CultureInfo]::InvariantCulture)+"`t"+$cpu.ToString('F6',[Globalization.CultureInfo]::InvariantCulture)+"`t"+$peak))
    $process.Dispose()
}
[IO.File]::WriteAllLines((Join-Path $repo 'results/preprocess/scale_benchmark.tsv'),$rows,$utf8)
$rows
