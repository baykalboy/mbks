#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <map>
#include <algorithm>

using namespace std;

const string BASE_PATH = "C:\\Users\\flash\\mbks\\for_alex\\1lab";
const string EXE_PATH = BASE_PATH + "\\vuln10.exe";
const string CONFIG_PATH = BASE_PATH + "\\config_10";
const string DEFAULT_CONFIG = BASE_PATH + "\\config_10_default";
const string DR_RUN = BASE_PATH + "\\DynamoRIO-Windows-11.91.20504\\bin32\\drrun.exe";

vector<uint8_t> BOUND_1 = {0x00, 0xFF, 0x7F, 0x80, 0x7E};
vector<uint16_t> BOUND_2 = {0x0000, 0xFFFF, 0x7FFF, 0x8000, 0x7FFE};
vector<uint32_t> BOUND_4 = {0x00000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x7FFFFFFE};

vector<uint8_t> ReadFile(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[-] Ошибка: не удалось открыть " << filename << "\n";
        return {};
    }
    return vector<uint8_t>((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

void WriteFile(const string& filename, const vector<uint8_t>& data) {
    ofstream file(filename, ios::binary);
    if (file) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

string RunTargetAndDebug() {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessA(EXE_PATH.c_str(), NULL, NULL, NULL, FALSE, 
        DEBUG_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return "ERROR: CreateProcess failed";
    }

    DEBUG_EVENT de;
    stringstream crash_info;
    
    int timeout_counter = 0;

    while (true) {
        if (!WaitForDebugEvent(&de, 100)) {
            timeout_counter++;
            if (timeout_counter > 50) { 
				crash_info << "Процесс завис, TerminateProc";
                TerminateProcess(pi.hProcess, 0);
                break;
            }
            continue;
        }
        timeout_counter = 0; 

        DWORD continueStatus = DBG_CONTINUE;

        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
			DWORD exitCode = de.u.ExitProcess.dwExitCode;
            if (exitCode != 0 && exitCode != 0x103 && crash_info.str().empty()) {
                crash_info << "FAST_FAIL: Процесс завершился с кодом 0x" << hex << exitCode;
            }
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
            break; 
        }
        
        if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            DWORD excCode = de.u.Exception.ExceptionRecord.ExceptionCode;
            
            // пропуск системных брейкпоинтов
            if (excCode == EXCEPTION_BREAKPOINT || excCode == 0x40010006 || excCode == 0x406D1388) {
                continueStatus = DBG_CONTINUE;
            } else {
                HANDLE hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, de.dwThreadId);
                if (hThread) {
                    CONTEXT ctx;
                    ctx.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(hThread, &ctx)) {
                        uint8_t stackDump[64] = {0};
                        SIZE_T bytesRead = 0;
                        ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(ctx.Esp), stackDump, sizeof(stackDump), &bytesRead);
                        
                        crash_info << "EXCEPTION_CODE: 0x" << hex << excCode 
                                   << " at 0x" << (uintptr_t)de.u.Exception.ExceptionRecord.ExceptionAddress << "\n"
                                   << "EAX: 0x" << ctx.Eax << " | EBX: 0x" << ctx.Ebx << " | ECX: 0x" << ctx.Ecx << " | EDX: 0x" << ctx.Edx << "\n"
                                   << "ESI: 0x" << ctx.Esi << " | EDI: 0x" << ctx.Edi << " | EBP: 0x" << ctx.Ebp << " | ESP: 0x" << ctx.Esp << "\n"
                                   << "EIP: 0x" << ctx.Eip << "\n"
                                   << "STACK DUMP: ";
                        for (int i = 0; i < bytesRead; ++i) {
															//дозаполнить маленькое число нулями спереди
                            crash_info << hex << setw(2) << setfill('0') << (int)stackDump[i] << " ";
                        }
                    }
                    CloseHandle(hThread);
                }
                
                TerminateProcess(pi.hProcess, 1);
                ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_EXCEPTION_NOT_HANDLED);
                break;
            }
        } else {
            
            continueStatus = DBG_CONTINUE;
        }
        
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continueStatus);
    }

    TerminateProcess(pi.hProcess, 0); 
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return crash_info.str();
}


string to_hex(int i) {
    stringstream stream;
    stream << hex << i;
    return stream.str();
}

void LogCrash(int iteration, const string& desc, const string& crash_report, const vector<uint8_t>& test_data) {
    cout << "\n[!] Крах на итерации " << iteration << "!\n";
    cout << "[-] Мутация: " << desc << "\n";
    cout << "[-] Подробная информация сохранена в logs\\crash_report.txt\n";
    
    string crash_file = BASE_PATH + "\\logs\\crash_" + to_string(iteration) + ".bin";
    WriteFile(crash_file, test_data);
    
    ofstream report(BASE_PATH + "\\logs\\crash_report.txt", ios::app);
	report << "--------------------------------------------------\n";
    report << "Крах на итерации " << iteration << "\n";
    report << "Входные параметры: " << desc << "\n";
    report << "Состояние системы:\n" << crash_report << "\n";
    report << "Бинарный файл: crash_" << iteration << ".bin\n";
    report << "--------------------------------------------------\n";
}


struct RunResult {
    int coverage;
    map<int, string> module_names;
    map<int, int> module_counts;
    bool crashed;
};


void LogCoverage(int iteration, const RunResult& res, const string& desc, const vector<uint8_t>& test_data) {
    ofstream log_cov(BASE_PATH + "\\logs\\coverage_history.txt", ios::app);
    log_cov << "[" << iteration << "]\n";
    
    for (auto it = res.module_names.begin(); it != res.module_names.end(); ++it) {
        int modId = it->first;
        string modName = it->second;
        int count = res.module_counts.at(modId);
        log_cov << modName << " = " << count << "\n";
    }
    log_cov << "Мутация: " << desc << "\n\n";
    
    WriteFile(BASE_PATH + "\\logs\\coverage_" + to_string(res.coverage) + ".bin", test_data);
}


RunResult ParseCoverageLog() {
    RunResult res;
    res.coverage = 0;
    res.crashed = false;

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((BASE_PATH + "\\*.log").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return res;
    string logPath = BASE_PATH + "\\" + findData.cFileName;
    FindClose(hFind);

    ifstream file(logPath);
    string line;
    bool inModules = false;

	vector<int> targetModIds;

    while (getline(file, line)) {
        if (line.find("Module Table") != string::npos) { inModules = true; continue; }
        if (line.find("BB Table") != string::npos) { inModules = false; continue; }

        if (inModules) {
            if (line.find("Columns:") != string::npos) continue;

            size_t firstComma = line.find(',');
            if (firstComma != string::npos) {
                try {
                    int modId = stoi(line.substr(0, firstComma));
                    
                    // вытаскиваем чистое имя файла из пути 
                    size_t lastComma = line.find_last_of(',');
                    if (lastComma != string::npos) {
                        string path = line.substr(lastComma + 1);
                        path.erase(0, path.find_first_not_of(" \t")); // убираем пробелы
                        size_t lastSlash = path.find_last_of("\\/");
                        string modName = (lastSlash != string::npos) ? path.substr(lastSlash + 1) : path;
                        modName.erase(modName.find_last_not_of("\r\n") + 1); // убираем перенос строки
                        
                        res.module_names[modId] = modName;
                        res.module_counts[modId] = 0; // изначально 0 блоков

						string lowerName = modName;
                        transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        if (lowerName.find("vuln10.exe") != string::npos || lowerName.find("func.dll") != string::npos) {
                            targetModIds.push_back(modId);
                        }
                    }
                } catch (...) {}
            }
        } else 
		{
            // считаем блоки для каждого зарегистрированного модуля
            if (line.find("module[") != string::npos) {
                size_t bracketOpen = line.find('[');
                size_t bracketClose = line.find(']');
                if (bracketOpen != string::npos && bracketClose != string::npos) {
                    try {
                        int modId = stoi(line.substr(bracketOpen + 1, bracketClose - bracketOpen - 1));
                        if (res.module_names.count(modId)) {
                            res.module_counts[modId]++;
                            
                            //увеличение счетчика только для func и vuln
                            if (find(targetModIds.begin(), targetModIds.end(), modId) != targetModIds.end()) {
                                res.coverage++;
                            }
                        }
                    } catch (...) {}
                }
            }
        }
    }
    
    file.close(); 
    DeleteFileA(logPath.c_str());

    return res;
}

RunResult RunAndAnalyze() {
    WIN32_FIND_DATAA findData; //структура для итеративного поиска
    HANDLE hFind = FindFirstFileA((BASE_PATH + "\\*.log").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do { DeleteFileA((BASE_PATH + "\\" + findData.cFileName).c_str()); } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    string cmdLine = DR_RUN + " -t drcov -dump_text -- " + EXE_PATH;
    char cmdBuf[512];
    strcpy(cmdBuf, cmdLine.c_str());

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, BASE_PATH.c_str(), &si, &pi)) {
        RunResult empty; 
		empty.coverage = 0; 
		empty.crashed = false; 
		return empty;
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 5000); 
    bool crashed = false;

    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0); 
    } else {
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
		   //0 - всё норм, 0x103 - процесс завершился, но еще память не очишена (STILL_ACTIVE), остальное - проверка варианта vuln (*a2 != 2)
        if (exitCode != 0 && exitCode != 0x103 && exitCode != 0xFFFFFFFF && exitCode != 4294967295) {
            crashed = true;
        }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    RunResult res = ParseCoverageLog();
    res.crashed = crashed;
    return res;
}


void RunSequentialMode(const vector<uint8_t>& original_data) {
    cout << "\n[+] Запуск последовательного режима \n";

    CreateDirectoryA((BASE_PATH + "\\logs").c_str(), NULL);
    int iteration = 0;
    size_t file_length = original_data.size();

    WriteFile(CONFIG_PATH, original_data);
    RunResult baseRes = RunAndAnalyze();
    int max_coverage = baseRes.coverage;
    cout << "[*] Базовое покрытие (Default Config): " << max_coverage << " блоков\n";
    vector<uint8_t> current_best_data = original_data;

    LogCoverage(0, baseRes, "Default Config", original_data);

    for (size_t offset = 0; offset < file_length; offset++) {
        
        for (size_t i = 0; i < BOUND_1.size(); i++) {
            iteration++;
            vector<uint8_t> test_data = current_best_data;
            test_data[offset] = BOUND_1[i];
            
            WriteFile(CONFIG_PATH, test_data);
            RunResult res = RunAndAnalyze();

            if (res.crashed) {
                string crash_report = RunTargetAndDebug();
                string desc = "Последовательная замена байт: 1-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex((int)BOUND_1[i]);
                LogCrash(iteration, desc, crash_report, test_data);
                // return;
            }
            
            if (res.coverage > max_coverage) {
                cout << "\n[+] Итерация " << iteration << ": НОВОЕ ПОКРЫТИЕ! " 
                     << max_coverage << " -> " << res.coverage << " (1-byte at 0x" << to_hex(offset) << ")\n";
                max_coverage = res.coverage;
                current_best_data = test_data;
                
                string desc = "1-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex((int)BOUND_1[i]);
                LogCoverage(iteration, res, desc, test_data);
            }
        }

        if (offset < file_length - 1) {
            for (size_t i = 0; i < BOUND_2.size(); i++) {
                iteration++;
                vector<uint8_t> test_data = current_best_data;
                *reinterpret_cast<uint16_t*>(&test_data[offset]) = BOUND_2[i];
                
                WriteFile(CONFIG_PATH, test_data);
                RunResult res = RunAndAnalyze();
                
                if (res.crashed) {
                    string crash_report = RunTargetAndDebug();
                    string desc = "Последовательная замена байт: 2-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex(BOUND_2[i]);
                    LogCrash(iteration, desc, crash_report, test_data);
                    // return; 
                }

                if (res.coverage > max_coverage) {
                    cout << "\n[+] Итерация " << iteration << ": НОВОЕ ПОКРЫТИЕ! " 
                        << max_coverage << " -> " << res.coverage << " (2-byte at 0x" << to_hex(offset) << ")\n";
                    max_coverage = res.coverage;
                    current_best_data = test_data;

                    string desc = "2-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex(BOUND_2[i]);
                    LogCoverage(iteration, res, desc, test_data);
                }
            }
        }

        if (offset < file_length - 3) {
            for (size_t i = 0; i < BOUND_4.size(); i++) {
                iteration++;
                vector<uint8_t> test_data = current_best_data;
                *reinterpret_cast<uint32_t*>(&test_data[offset]) = BOUND_4[i];
                
                WriteFile(CONFIG_PATH, test_data);
                RunResult res = RunAndAnalyze();
                
                if (res.crashed) {
                    string crash_report = RunTargetAndDebug();
                    string desc = "Последовательная замена байт: 4-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex(BOUND_4[i]);
                    LogCrash(iteration, desc, crash_report, test_data);
                    // return;
                }

                if (res.coverage > max_coverage) {
                    cout << "\n[+] Итерация " << iteration << ": НОВОЕ ПОКРЫТИЕ! " 
                        << max_coverage << " -> " << res.coverage << " (4-byte at 0x" << to_hex(offset) << ")\n";
                    max_coverage = res.coverage;
                    current_best_data = test_data;

                    string desc = "4-byte at 0x" + to_hex(offset) + " -> 0x" + to_hex(BOUND_4[i]);
                    LogCoverage(iteration, res, desc, test_data);
                }
            }
        }

        if (offset % 20 == 0) {
            cout << "Проверено смещений: " << offset << "/" << file_length 
                 << " (Итераций: " << iteration << ")\n";
        }
    }

    cout << "\n\n[!] Последовательный перебор завершен. Проверено " << iteration << " вариантов).\n";
}

void RunSmartMode(const vector<uint8_t>& original_data) {
    cout << "\n[+] Запуск автоматического режима\n";
    
    string target_delim = "/start";
    vector<size_t> delimiter_positions;
    
    auto it = original_data.begin();
    while ((it = search(it, original_data.end(), target_delim.begin(), target_delim.end())) != original_data.end()) {
        // запоминаем позицию ПОСЛЕДНЕГО символа 't' в строке "/start"
        size_t pos = distance(original_data.begin(), it) + target_delim.length() - 1;
        delimiter_positions.push_back(pos);
        it += target_delim.length();
    }
    
    cout << "[*] Найдено маркеров '" << target_delim << "' в файле: " << delimiter_positions.size() << "\n";

    CreateDirectoryA((BASE_PATH + "\\logs").c_str(), NULL);

    WriteFile(CONFIG_PATH, original_data);
    RunResult baseRes = RunAndAnalyze();
    int max_coverage = baseRes.coverage;
    cout << "[*] Базовое покрытие: " << max_coverage << " блоков\n";
    LogCoverage(0, baseRes, "Автоматический режим: Default Config", original_data);

    // (unsigned __int16)
    vector<uint32_t> target_lengths = {10, 50, 128, 512, 1024, 2048, 2500, 2600, 3000, 5000, 66000, 68000, 70000};
    int iteration = 0;

    for (size_t pos : delimiter_positions) {
        
        for (uint32_t length : target_lengths) {
            iteration++;
            
            vector<uint8_t> test_data = original_data; 
            string desc;
            
            if (test_data.size() >= 12) {
                *reinterpret_cast<uint32_t*>(&test_data[4]) = length;
                *reinterpret_cast<uint32_t*>(&test_data[8]) = 0;
            }
            
            vector<uint8_t> payload(length, 0x41); 
            
            if (pos < test_data.size()) {
                test_data.insert(test_data.begin() + pos + 1, payload.begin(), payload.end());
                desc = "Автоматический: dst_len=" + to_string(length) + " + insert after delim 0x" + to_hex(pos);
            } else {
                test_data.insert(test_data.end(), payload.begin(), payload.end());
                desc = "Автоматический режим: dst_len=" + to_string(length) + " + append to EOF";
            }

            WriteFile(CONFIG_PATH, test_data);
            
            RunResult res = RunAndAnalyze();
            
            if (res.crashed) {
                cout << "\n[!] Итерация " << iteration << ": Крах программы! Сохранение логов\n";
                string crash_report = RunTargetAndDebug(); 
                LogCrash(iteration, desc, crash_report, test_data);
                LogCoverage(iteration, res, "CRASH TRIGGERED: " + desc, test_data);          
                continue; 
            }

            if (res.coverage > max_coverage) {
                cout << "\n[+] Итерация " << iteration << ": НОВОЕ ПОКРЫТИЕ! " << max_coverage << " -> " << res.coverage << " блоков (" << desc << ")\n";
                max_coverage = res.coverage;
                
                LogCoverage(iteration, res, desc, test_data);
            }
        }
        
    }

    cout << "\n[!] Структурный перебор завершен. Всего итераций: " << iteration << "\n";
}

void SearchDelimeter(vector<uint8_t>& original_data) {

	cout << "\n[+] Поиск количества разделителей (, : = ; /)\n";
    
    vector<uint8_t> delims = {',', ':', '=', ';', '/'}; 
    vector<size_t> delimiter_positions;
    
    for (size_t i = 0; i < original_data.size(); i++) {
        for (size_t j = 0; j < delims.size(); j++) {
            if (original_data[i] == delims[j]) {
                delimiter_positions.push_back(i);
                break;
            }
        }
    }
    cout << "[*] Найдено разделителей в файле: " << delimiter_positions.size() << "\n";

}

int GetRandomInt(int min, int max) {
    return min + (rand() % (max - min + 1));
}

void RunRandomMode(const vector<uint8_t>& original_data, int iterations) {
    cout << "\n[+] Запуск режима случайных мутаций\n";
    CreateDirectoryA((BASE_PATH + "\\logs").c_str(), NULL);

    srand((unsigned int)time(NULL));
    
    WriteFile(CONFIG_PATH, original_data);
    RunResult baseRes = RunAndAnalyze();
    int max_coverage = baseRes.coverage;
    
    vector<uint8_t> current_best_data = original_data; 
    
    cout << "[*] Базовое целевое покрытие: " << max_coverage << " блоков\n";
    cout << "[*] Запланировано итераций: " << iterations << "\n";
    LogCoverage(0, baseRes, "Random Mode: Default Config", original_data);

    for (int i = 1; i <= iterations; i++) {
        vector<uint8_t> test_data = current_best_data; 
        //колво мутаций за итерацию
        int num_mutations = GetRandomInt(1, 3);
        
        for (int m = 0; m < num_mutations; m++) {
            int mutation_type = GetRandomInt(0, 2);
            size_t target_pos = GetRandomInt(0, test_data.size() - 1);
            
            switch(mutation_type) {
                case 0: //просто рандомный байт
                    test_data[target_pos] = (uint8_t)GetRandomInt(0, 255);
                    break;
                case 1: // инверсия одного случайного бита
                    test_data[target_pos] ^= (1 << GetRandomInt(0, 7));
                    break;
                case 2: //вставка граничного
                    uint8_t magic[] = {0x00, 0xFF, 0x7F, 0x80};
                    test_data[target_pos] = BOUND_1[GetRandomInt(0, 4)];
                    break;
            }
        }

        string desc = "Random: " + to_string(num_mutations) + " mutations applied";
        WriteFile(CONFIG_PATH, test_data);
        
        RunResult res = RunAndAnalyze();
        
        if (res.crashed) {
            cout << "\n[!] Итерация " << i << ": Крах программы! Сохраняем логи\n";
            string crash_report = RunTargetAndDebug(); 
            LogCrash(i, desc, crash_report, test_data);
            LogCoverage(i, res, "CRASH TRIGGERED: " + desc, test_data);
            continue; 
        }

        if (res.coverage > max_coverage) {
            cout << "\n[+] Итерация " << i << ": НОВОЕ ПОКРЫТИЕ! " << max_coverage << " -> " << res.coverage << " блоков\n";
            max_coverage = res.coverage;
            current_best_data = test_data; // Естественный отбор в действии!
            LogCoverage(i, res, desc, test_data);
        }
        
        if (i % 20 == 0) cout << "Пройдено итераций: " << i << "/" << iterations << "\r";
    }
    
    cout << "\n\n[!] Случайный перебор завершен.\n";
}

void RunManualMode(const vector<uint8_t>& original_data) {
    cout << "\n[+] Запуск ручного режима (Manual Mode)...\n";
    CreateDirectoryA((BASE_PATH + "\\logs").c_str(), NULL);

    vector<uint8_t> current_data = original_data;
    
    WriteFile(CONFIG_PATH, current_data);
    RunResult baseRes = RunAndAnalyze();
    int current_coverage = baseRes.coverage;
    cout << "[*] Базовое целевое покрытие: " << current_coverage << " блоков\n";

    string input;
    int manual_iteration = 0;

    while (true) {
        manual_iteration++;
        cout << "Введите смещение (offset) в HEX (например, 8 для 0x08) или 'q' для выхода: ";
        cin >> input;
        
        if (input == "q" || input == "Q") break;

        size_t offset = 0;
        try {
            offset = stoull(input, nullptr, 16); // парсим строку как 16-ричное число
        } catch (...) {
            cout << "[-] Ошибка: неверный формат смещения. Вводите только шестнадцатеричные символы.\n";
            continue;
        }

        if (offset >= current_data.size()) {
            cout << "[-] Ошибка: смещение 0x" << hex << offset << " выходит за пределы файла (размер 0x" << current_data.size() << dec << ").\n";
            continue;
        }

        cout << "Введите размер заменяемых данных в байтах (1, 2, 4): ";
        int size = 0;
        cin >> dec >> size;

        if (size != 1 && size != 2 && size != 4) {
            cout << "[-] Ошибка: поддерживается только 1, 2 или 4 байта.\n";
            continue;
        }

        if (offset + size > current_data.size()) {
            cout << "[-] Ошибка: данные выйдут за пределы файла.\n";
            continue;
        }

        cout << "Введите новое значение в HEX: ";
        cin >> input;

        unsigned int new_value = 0;
        try {
            new_value = stoul(input, nullptr, 16);
        } catch (...) {
            cout << "[-] Ошибка: неверный формат значения.\n";
            continue;
        }

        vector<uint8_t> test_data = current_data;

        // применяем мутацию (с учетом размера)
        if (size == 1) {
            test_data[offset] = (uint8_t)new_value;
        } else if (size == 2) {
            *reinterpret_cast<uint16_t*>(&test_data[offset]) = (uint16_t)new_value;
        } else if (size == 4) {
            *reinterpret_cast<uint32_t*>(&test_data[offset]) = (uint32_t)new_value;
        }

        stringstream ss_desc;
        ss_desc << "Manual: " << size << "-byte at 0x" << hex << offset << " -> 0x" << new_value;
        string desc = ss_desc.str();
        
        cout << "[*] Тестируем мутацию: " << desc << "\n";

        WriteFile(CONFIG_PATH, test_data);
        RunResult res = RunAndAnalyze();

        if (res.crashed) {
            cout << "\n[!] ПРОГРАММА УПАЛА! Собираем логи...\n";
            string crash_report = RunTargetAndDebug();
            LogCrash(manual_iteration, desc, crash_report, test_data);
            LogCoverage(manual_iteration, res, "CRASH TRIGGERED: " + desc, test_data);
            cout << "[*] Логи сохранены в папку logs. Возврат к безопасному состоянию файла.\n";
        } else {
            cout << "[+] Покрытие: " << dec << res.coverage << " блоков ";
            if (res.coverage > current_coverage) {
                cout << "(УВЕЛИЧИЛОСЬ! +" << (res.coverage - current_coverage) << ")\n";
            } else if (res.coverage < current_coverage) {
                cout << "(уменьшилось, -" << (current_coverage - res.coverage) << ")\n";
            } else {
                cout << "(без изменений)\n";
            }
            
            LogCoverage(manual_iteration, res, desc, test_data);

            cout << "Сохранить это изменение в файле для следующих проверок? (y/n): ";
            char save_choice;
            cin >> save_choice;
            if (save_choice == 'y' || save_choice == 'Y') {
                current_data = test_data;
                current_coverage = res.coverage;
                cout << "[+] Изменение зафиксировано.\n";
            } else {
                cout << "[-] Откат изменения.\n";
            }
        }
    }
    
    // В конце восстанавливаем дефолтный конфиг
    WriteFile(CONFIG_PATH, original_data);
    cout << "\n[!] Ручной режим завершен.\n";
}

void RunAdaptiveMode(const vector<uint8_t>& original_data, int iterations) {
    cout << "\n[+] Запуск Адаптивного режима\n";
    CreateDirectoryA((BASE_PATH + "\\logs").c_str(), NULL);
    srand((unsigned int)time(NULL));
    
    WriteFile(CONFIG_PATH, original_data);
    RunResult baseRes = RunAndAnalyze();
    int max_coverage = baseRes.coverage;
    vector<uint8_t> current_best_data = original_data; 
    
    cout << "[*] Базовое покрытие: " << max_coverage << " блоков\n";

    int current_strategy = 1; 
    int degradation_counter = 0;
    const int DEGRADATION_THRESHOLD = 7; // порог смены алгоритма

    for (int i = 1; i <= iterations; i++) {
        vector<uint8_t> test_data = current_best_data; 
        string desc;
        size_t target_pos = GetRandomInt(0, test_data.size() - 1);
        
        if (current_strategy == 0) {
            // стратегия 0: осторожная (дозапись в конец / вставка чисел)
            vector<uint32_t> lengths =  {10, 50, 128, 512, 1024, 2048, 2500, 2600, 3000, 5000, 66000, 68000, 70000};
            uint32_t len = lengths[GetRandomInt(0, lengths.size() - 1)];
            if (test_data.size() >= 12) {
                *reinterpret_cast<uint32_t*>(&test_data[4]) = len;
                *reinterpret_cast<uint32_t*>(&test_data[8]) = 0;
            }
            vector<uint8_t> payload(GetRandomInt(100, 500), 0x41);
            test_data.insert(test_data.end(), payload.begin(), payload.end());
            desc = "Стратегия 0: Вставка числа " + to_string(len);
            
        } else if (current_strategy == 1) {
            // стратегия 1: агрессивная (граничные значения)
            test_data[target_pos] = BOUND_1[GetRandomInt(0, BOUND_1.size() - 1)];
            desc = "Стратегия 1: Граничное значение по смещению 0x" + to_hex(target_pos);
            
        } else if (current_strategy == 2) {
            // стратегия 2: хаотичная (битовые инверсии)
            test_data[target_pos] ^= (1 << GetRandomInt(0, 7));
            desc = "Стратегия 2: Битовая инверсия по смещению 0x" + to_hex(target_pos);
        }

        WriteFile(CONFIG_PATH, test_data);
        RunResult res = RunAndAnalyze();
        
        if (res.crashed) {
            cout << "\n[!] Итерация " << i << ": Крах программы!\n";
            string crash_report = RunTargetAndDebug(); 
            LogCrash(i, desc, crash_report, test_data);
            // continue; 
        }

        if (res.coverage > max_coverage) {
            cout << "\n[+] Итерация " << i << ": НОВОЕ ПОКРЫТИЕ! " << max_coverage << " -> " << res.coverage << " блоков (" << desc << ")\n";
            max_coverage = res.coverage;
            current_best_data = test_data;
            degradation_counter = 0; // сбрасываем счетчик, так как алгоритм успешен
            LogCoverage(i, res, desc, test_data);
            
        } else if (res.coverage <= max_coverage) {
            // покрытие упало, Фаззер сломал файл.
            degradation_counter++;
            
            // если слишком много неудач - меняем алгоритм работы
            if (degradation_counter >= DEGRADATION_THRESHOLD) {
                current_strategy = (current_strategy + 1) % 3; // Переключаем 0 -> 1 -> 2 -> 0
                cout << "\n[~] Адаптация: Покрытие стагнирует/падает. Переключение на Стратегию " << current_strategy << "\n";
                degradation_counter = 0; // сбрасываем счетчик после смены
            }
        }
        
        if (i % 10 == 0) cout << "Итерация: " << i << "/" << iterations << " | Стратегия: " << current_strategy << "\n";
    }
    cout << "\n Максимальное достигнутое покрытие: " << to_string(max_coverage); 
    cout << "\n\n[!] Адаптивный перебор завершен.\n";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    
    vector<uint8_t> original_data = ReadFile(DEFAULT_CONFIG);
    if (original_data.size() == 0) {
        cout << "[-] Дефолтный конфиг не найден!\n";
        return 1;
    }
	int it = 100;
    while (true) {
        cout 
             << "---------------------------------------\n"
             << "1. Последовательная замена байт\n"
             << "2. Автоматический режим\n"
			 << "3. Случайные мутации\n"
			 << "4. Поиск разделителей в файле\n"
			 << "5. Ручной режим\n"
             << "6. Адаптивный режим\n"
             << "0. Выход\n"
             << "----------------------------------------\n"
             << "Выберите режим: ";
            
        int choice;
        cin >> choice;
        
        if (choice == 1) {
            RunSequentialMode(original_data);
        } else if (choice == 2) {
            RunSmartMode(original_data);
        } else if (choice == 3) {
			RunRandomMode(original_data, it);
		} else if (choice == 4) {
			SearchDelimeter(original_data);
		} else if (choice == 5) {
			RunManualMode(original_data); 
        } else if (choice == 6) {
            RunAdaptiveMode(original_data, it);
		} else if (choice == 0) {
            cout << "Выход\n";
            break;
        } else {
            cout << "Неверный ввод.\n";
        }
    }
    return 0;
}