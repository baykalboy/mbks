CODEQL      = C:/Users/flash/AppData/Roaming/Code/User/globalStorage/github.vscode-codeql/distribution2/codeql/codeql.exe
CODEQL_DB   = dnn-codeql-db

SONAR_URL   = http://localhost:9000
SONAR_KEY   = dnn-910
SONAR_TOKEN = sqa_f39a59627b4e0f10a467b533808a173f6ab5b0ac
SONAR_EXT   = $(shell cygpath -m "$(CURDIR)")/sonarqube-rules/cve-2017-9822-external-issues.json
MSBUILD     = C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe
SONAR_BAT   = C:/sonarqube/sonarqube-26.4.0.121862/bin/windows-x86-64/StartSonar.bat


codeql-db:
	rm -rf $(CODEQL_DB)
	"$(CODEQL)" database create $(CODEQL_DB) --language=csharp --source-root=dnn-910 --build-mode=none

# кастомный запрос CVE-2017-9822, в SARIF-файл
codeql-custom:
	"$(CODEQL)" database analyze $(CODEQL_DB) codeql-queries/CVE-2017-9822-DnnDeserialization.ql --format=sarif-latest --output=codeql-results/cve-2017-9822-custom.sarif --rerun

# встроенные правила безопасности, C# в SARIF-файл
codeql-builtin:
	"$(CODEQL)" database analyze $(CODEQL_DB) "codeql/csharp-queries:codeql-suites/csharp-security-extended.qls" --format=sarif-latest --output=codeql-results/dnn-security.sarif --rerun



semgrep-csharp:
	PYTHONUTF8=1 semgrep --config p/csharp dnn-910/ --sarif -o semgrep-results/dnn-builtin.sarif


semgrep-custom:
	PYTHONUTF8=1 semgrep --config semgrep-rules/cve-2017-9822.yaml dnn-910/ --sarif -o semgrep-results/cve-2017-9822-semgrep.sarif


semgrep-show:
	PYTHONUTF8=1 semgrep --config semgrep-rules/cve-2017-9822.yaml dnn-910/

semgrep: semgrep-custom semgrep-show


sonar-start:
	powershell -Command "& '$(SONAR_BAT)'"


sonar-begin:
	powershell -Command "dotnet-sonarscanner begin /k:$(SONAR_KEY) /d:sonar.host.url=$(SONAR_URL) /d:sonar.token=$(SONAR_TOKEN) /d:sonar.externalIssuesReportPaths=$(SONAR_EXT) /d:sonar.scm.disabled=true"


sonar-build:
	powershell -Command "& '$(MSBUILD)' 'dnn-910/DNN Platform/Library/DotNetNuke.Library.csproj' /p:Configuration=Debug /p:RestorePackages=false /p:EnableNuGetPackageRestore=false /v:m"

sonar-end:
	powershell -Command "dotnet-sonarscanner end /d:sonar.token=$(SONAR_TOKEN)"

sonar: sonar-begin sonar-build sonar-end
