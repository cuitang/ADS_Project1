$ErrorActionPreference = 'Stop'

# Native CLI paths stay relative to avoid Chinese argv encoding issues on Windows.
$projectRoot = Join-Path $PSScriptRoot '../..'
Push-Location $projectRoot

try {
    $exe = '.\build\search.exe'
    $index = 'data/index'
    $stops = 'config/stopwords.txt'
    $resultPath = 'results/query/threshold_results.csv'

    foreach ($path in @($exe, "$index/metadata.tsv", $stops)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing file: $path. Build the executable and index first."
        }
    }

    # Regression baselines for the current 912-document corpus and stopword list.
    $groups = @(
        @{ Mode = 'word'; Query = 'king'; Thresholds = @('1', '2', '5', '10', '20'); Counts = @(377, 262, 137, 50, 10) },
        @{ Mode = 'word'; Query = 'love'; Thresholds = @('1', '2', '5', '10', '20'); Counts = @(604, 425, 191, 78, 20) },
        @{ Mode = 'word'; Query = 'run'; Thresholds = @('1', '2', '5', '10', '20'); Counts = @(184, 53, 6, 1, 0) },
        @{ Mode = 'multi'; Query = 'king queen crown'; Thresholds = @('0.30', '0.50', '1.00'); Counts = @(459, 214, 86) },
        @{ Mode = 'multi'; Query = 'love death life'; Thresholds = @('0.30', '0.50', '1.00'); Counts = @(744, 440, 205) },
        @{ Mode = 'multi'; Query = 'run walk speak'; Thresholds = @('0.30', '0.50', '1.00'); Counts = @(543, 205, 42) }
    )

    $results = @()

    foreach ($group in $groups) {
        $previousIds = $null

        for ($i = 0; $i -lt $group.Thresholds.Count; $i++) {
            $threshold = $group.Thresholds[$i]
            $cliArgs = @($group.Mode) + ($group.Query -split ' ')

            if ($group.Mode -eq 'word') {
                $cliArgs += @('--t', $threshold)
            }
            else {
                $cliArgs += @('--tau', $threshold)
            }

            $cliArgs += @('--index', $index, '--stops', $stops, '--csv')

            # Windows PowerShell 5.1 may wrap native stderr in ErrorRecord objects.
            # Capture diagnostics without treating the normal index-load log as failure.
            try {
                $ErrorActionPreference = 'Continue'
                $output = @(& $exe @cliArgs 2>&1)
                $exitCode = $LASTEXITCODE
            }
            finally {
                $ErrorActionPreference = 'Stop'
            }

            if ($exitCode -ne 0) {
                throw "Query failed (exit=$exitCode): $($group.Query)`n$($output -join "`n")"
            }

            $prefix = "$($group.Mode),$($group.Query),"
            $csvLines = @($output | ForEach-Object { $_.ToString().Trim() } |
                Where-Object { $_.StartsWith($prefix) })

            if ($csvLines.Count -ne 1) {
                throw "Expected one CSV result: $($output -join "`n")"
            }

            $fields = $csvLines[0] -split ','
            if ($fields.Count -ne 6 -or $fields[2] -cne $threshold) {
                throw "Unexpected CSV result: $($csvLines[0])"
            }

            $count = [int]$fields[3]
            $ids = @()
            if ($fields[4] -ne '') {
                $ids = @($fields[4] -split ';' | ForEach-Object { [int]$_ })
            }

            if ($count -ne $group.Counts[$i] -or $ids.Count -ne $count) {
                throw "Count mismatch: $($group.Query), threshold=$threshold, expected=$($group.Counts[$i]), actual=$count"
            }

            if (@($ids | Sort-Object -Unique).Count -ne $count) {
                throw "Duplicate document IDs: $($group.Query), threshold=$threshold"
            }

            # A higher threshold must never introduce a new document.
            if ($null -ne $previousIds) {
                foreach ($id in $ids) {
                    if ($previousIds -notcontains $id) {
                        throw "Subset check failed: $($group.Query), threshold=$threshold, new document=$id"
                    }
                }
            }
            $previousIds = $ids

            $results += [PSCustomObject]@{
                mode = $fields[0]
                query = $fields[1]
                threshold = $fields[2]
                count = $count
                doc_ids = $fields[4]
                query_seconds = $fields[5]
            }
            Write-Output "PASS: $($group.Mode), $($group.Query), threshold=$threshold, documents=$count"
        }
    }

    New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
    $results | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding UTF8
    Write-Output "All $($results.Count) threshold tests passed. Results: $resultPath"
}
finally {
    Pop-Location
}
