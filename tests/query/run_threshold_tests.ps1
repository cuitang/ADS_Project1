$ErrorActionPreference = 'Stop'

$exe = (Resolve-Path 'build/search.exe').Path
$index = (Resolve-Path 'data/test/index').Path
$stops = (Resolve-Path 'data/test/demo_stopwords.txt').Path

$cases = @(
    @{
        Mode      = 'word'
        Query     = 'king'
        Threshold = '1'
        Expected  = '1;2;4'
    },
    @{
        Mode      = 'word'
        Query     = 'king'
        Threshold = '2'
        Expected  = ''
    },
    @{
        Mode      = 'multi'
        Query     = 'king queen run'
        Threshold = '0.30'
        Expected  = '1;2;3;4'
    },
    @{
        Mode      = 'multi'
        Query     = 'king queen run'
        Threshold = '0.50'
        Expected  = '1;3;4'
    },
    @{
        Mode      = 'multi'
        Query     = 'king queen run'
        Threshold = '1.00'
        Expected  = '4'
    }
)

foreach ($case in $cases) {
    $args = @($case.Mode) + ($case.Query -split ' ')

    if ($case.Mode -eq 'word') {
        $args += @('--t', $case.Threshold)
    }
    else {
        $args += @('--tau', $case.Threshold)
    }

    $args += @(
        '--index', $index,
        '--stops', $stops,
        '--csv'
    )

    $stdout = @(& $exe @args 2>$null)

    if ($LASTEXITCODE -ne 0) {
        throw "查询执行失败：$($case.Mode) $($case.Query)"
    }

    if ($stdout.Count -eq 0) {
        throw "查询没有输出结果：$($case.Mode) $($case.Query)"
    }

    $line = ($stdout | Select-Object -Last 1).ToString().Trim()
    $fields = $line -split ','

    if ($fields.Count -lt 6) {
        throw "CSV 输出格式错误：$line"
    }

    if ([string]::IsNullOrEmpty($case.Expected)) {
        $expectedCount = 0
    }
    else {
        $expectedCount = ($case.Expected -split ';').Count
    }

    $expectedPrefix = @(
        $case.Mode
        $case.Query
        $case.Threshold
        $expectedCount
        $case.Expected
    ) -join ','

    $actualPrefix = ($fields[0..4] -join ',')

    if ($actualPrefix -ne $expectedPrefix) {
        throw @"
阈值测试失败
查询：$($case.Mode) $($case.Query)
阈值：$($case.Threshold)
预期：$expectedPrefix
实际：$actualPrefix
"@
    }

    Write-Output "PASS: $expectedPrefix"
}

Write-Output '所有查询阈值测试通过。'