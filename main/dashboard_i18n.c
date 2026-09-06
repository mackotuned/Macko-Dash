#include "dashboard_i18n.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *english;
    const char *spanish;
    const char *portuguese;
    const char *japanese;
} translation_t;

static const translation_t TRANSLATIONS[] = {
    {"SETTINGS", "AJUSTES", "CONFIGURAÇÕES", "設定"},
    {"SYSTEM SETTINGS", "AJUSTES DEL SISTEMA", "CONFIGURAÇÕES DO SISTEMA", "システム設定"},
    {"THEME", "TEMA", "TEMA", "テーマ"},
    {"DISPLAY", "PANTALLA", "TELA", "ディスプレイ"},
    {"UNITS", "UNIDADES", "UNIDADES", "単位"},
    {"Units", "Unidades", "Unidades", "単位"},
    {"SYSTEM", "SISTEMA", "SISTEMA", "システム"},
    {"ENGINE LIMITS", "LÍMITES DEL MOTOR", "LIMITES DO MOTOR", "エンジン制限"},
    {"ECU / CAN PROTOCOL", "PROTOCOLO ECU / CAN", "PROTOCOLO ECU / CAN", "ECU / CAN プロトコル"},
    {"DIAGNOSTICS", "DIAGNÓSTICO", "DIAGNÓSTICOS", "診断"},
    {"SESSION PEAKS", "MÁXIMOS DE SESIÓN", "PICOS DA SESSÃO", "セッション最大値"},
    {"DRIVING LOGS", "REGISTROS DE CONDUCCIÓN", "REGISTROS DE CONDUÇÃO", "走行ログ"},
    {"CONTACT & SUPPORT", "CONTACTO Y SOPORTE", "CONTATO E SUPORTE", "連絡先とサポート"},
    {"Contact & Support", "Contacto y soporte", "Contato e suporte", "連絡先とサポート"},
    {"SHORTCUTS & TIPS", "ATAJOS Y CONSEJOS", "ATALHOS E DICAS", "ショートカットとヒント"},
    {"Shortcuts & Tips", "Atajos y consejos", "Atalhos e dicas", "ショートカットとヒント"},
    {"READ ME", "LEER PRIMERO", "LEIA-ME", "はじめに"},
    {"Read Me", "Leer primero", "Leia-me", "はじめに"},
    {"ODOMETER & TRIPS", "ODÓMETRO Y VIAJES", "HODÔMETRO E VIAGENS", "走行距離とトリップ"},
    {"ACHIEVEMENTS", "LOGROS", "CONQUISTAS", "実績"},
    {"Brightness", "Brillo", "Brilho", "明るさ"},
    {"RPM / Speed Smoothing", "Suavizado de RPM / velocidad", "Suavização de RPM / velocidade", "RPM / 速度スムージング"},
    {"Off\nLight\nMedium\nStrong", "Desactivado\nLeve\nMedio\nFuerte", "Desativado\nLeve\nMédio\nForte", "オフ\n弱\n中\n強"},
    {"Show Simulation Button", "Mostrar botón de simulación", "Mostrar botão de simulação", "シミュレーションボタンを表示"},
    {"Road Speed", "Velocidad", "Velocidade", "車速"},
    {"Temperature", "Temperatura", "Temperatura", "温度"},
    {"Pressure", "Presión", "Pressão", "圧力"},
    {"Distance & Odometer", "Distancia y odómetro", "Distância e hodômetro", "距離と走行距離"},
    {"Diagnostics", "Diagnóstico", "Diagnósticos", "診断"},
    {"Language", "Idioma", "Idioma", "言語"},
    {"Boot Logo", "Logo de inicio", "Logo de inicialização", "起動ロゴ"},
    {"Fuel Gauge Setup", "Configuración del medidor de combustible", "Configuração do medidor de combustível", "燃料計設定"},
    {"FUEL GAUGE SETUP", "CONFIGURACIÓN DEL MEDIDOR DE COMBUSTIBLE", "CONFIGURAÇÃO DO MEDIDOR DE COMBUSTÍVEL", "燃料計設定"},
    {"VEHICLE SETUP", "CONFIGURACIÓN DEL VEHÍCULO", "CONFIGURAÇÃO DO VEÍCULO", "車設定"},
    {"Hondata Analog Input", "Entrada analógica Hondata", "Entrada analógica Hondata", "Hondata Analog"},
    {"Empty voltage", "Voltaje vacío", "Tensão vazio", "0% 電圧"},
    {"Full voltage", "Voltaje lleno", "Tensão cheio", "満タン電圧"},
    {"Live Reading", "Lectura en vivo", "Leitura ao vivo", "ライブ値"},
    {"Hondata analog inputs must not exceed 5 V.", "Las entradas analógicas Hondata no deben superar 5 V.", "As entradas analógicas Hondata não devem exceder 5 V.", "Hondata Analog 5 V 最大"},
    {"Built-in MackoDash", "MackoDash integrado", "MackoDash integrado", "標準 MackoDash"},
    {"Changes appear on the next startup.", "Los cambios aparecen en el próximo inicio.", "As alterações aparecem na próxima inicialização.", "変更は次回起動時に表示されます。"},
    {"Selected. Shown on next startup.", "Seleccionado. Se mostrará en el próximo inicio.", "Selecionado. Será mostrado na próxima inicialização.", "選択しました。次回起動時に表示されます。"},
    {"Could not save selection.", "No se pudo guardar la selección.", "Não foi possível salvar a seleção.", "選択を保存できませんでした。"},
    {"Boot logo deleted.", "Logo de inicio eliminado.", "Logo de inicialização excluído.", "起動ロゴを削除しました。"},
    {"Could not delete boot logo.", "No se pudo eliminar el logo de inicio.", "Não foi possível excluir o logo de inicialização.", "起動ロゴを削除できませんでした。"},
    {"English\nEspañol\nPortuguês\n日本語", "English\nEspañol\nPortuguês\n日本語", "English\nEspañol\nPortuguês\n日本語", "English\nEspañol\nPortuguês\n日本語"},
    {"GEAR", "MARCHA", "MARCHA", "ギア"},
    {"FUEL", "COMBUSTIBLE", "COMBUSTÍVEL", "燃料"},
    {"SHIFT", "CAMBIO", "TROCA", "シフト"},
    {"COOLANT TEMP", "TEMP. REFRIGERANTE", "TEMP. DO MOTOR", "冷却水温"},
    {"INTAKE AIR TEMP", "TEMP. DE ADMISIÓN", "TEMP. DO AR", "吸気温"},
    {"IGNITION TIMING", "AVANCE DE ENCENDIDO", "PONTO DE IGNIÇÃO", "点火時期"},
    {"BATTERY", "BATERÍA", "BATERIA", "バッテリー"},
    {"THROTTLE POS", "POS. ACELERADOR", "POS. BORBOLETA", "スロットル開度"},
    {"OIL PRESSURE", "PRESIÓN DE ACEITE", "PRESSÃO DO ÓLEO", "油圧"},
    {"INJECTOR DUTY", "CICLO DEL INYECTOR", "CICLO DO INJETOR", "インジェクター開度"},
    {"KNOCK RETARD", "RETARDO POR DETONACIÓN", "ATRASO POR DETONAÇÃO", "ノックリタード"},
    {"ECU DISCONNECTED", "ECU DESCONECTADA", "ECU DESCONECTADA", "ECU 未接続"},
    {"ENGINE WARNING", "ALERTA DEL MOTOR", "ALERTA DO MOTOR", "エンジン警告"},
    {"SIMULATION MODE", "MODO DE SIMULACIÓN", "MODO DE SIMULAÇÃO", "シミュレーションモード"},
    {"Cancel", "Cancelar", "Cancelar", "キャンセル"},
    {"Save", "Guardar", "Salvar", "保存"},
    {"Set", "Aplicar", "Aplicar", "適用"},
    {"Delete", "Eliminar", "Excluir", "削除"},
    {"Start", "Iniciar", "Iniciar", "開始"},
    {"Stop", "Detener", "Parar", "停止"},
    {"Reset", "Restablecer", "Redefinir", "リセット"},
    {"Reset Session", "Restablecer sesión", "Redefinir sessão", "セッションをリセット"},
    {"Reset Everything", "Restablecer todo", "Redefinir tudo", "すべてリセット"},
    {"This cannot be undone.", "Esto no se puede deshacer.", "Isto não pode ser desfeito.", "この操作は元に戻せません。"},
    {"FACTORY RESET?", "¿RESTABLECER DE FÁBRICA?", "RESTAURAR PADRÕES?", "工場出荷状態に戻しますか？"},
    {"DELETE THEME?", "¿ELIMINAR TEMA?", "EXCLUIR TEMA?", "テーマを削除しますか？"},
    {"Could not delete the theme from the SD card.", "No se pudo eliminar el tema de la tarjeta SD.", "Não foi possível excluir o tema do cartão SD.", "SDカードからテーマを削除できませんでした。"},
    {"IMPORTED", "IMPORTADO", "IMPORTADO", "インポート済み"},
    {"Auto Record", "Grabación automática", "Gravação automática", "自動記録"},
    {"Next Log Name", "Nombre del próximo registro", "Nome do próximo registro", "次のログ名"},
    {"Select a recorded log", "Seleccione un registro", "Selecione um registro", "記録済みログを選択"},
    {"Touch a graph for exact values", "Toque un gráfico para ver valores exactos", "Toque no gráfico para valores exatos", "グラフをタッチして正確な値を表示"},
    {"Loading log...", "Cargando registro...", "Carregando registro...", "ログを読み込み中..."},
    {"No logs found", "No se encontraron registros", "Nenhum registro encontrado", "ログが見つかりません"},
    {"No driving logs found on the SD card.", "No se encontraron registros en la tarjeta SD.", "Nenhum registro encontrado no cartão SD.", "SDカードに走行ログがありません。"},
    {"Waiting", "Esperando", "Aguardando", "待機中"},
    {"Selected", "Seleccionado", "Selecionado", "選択済み"},
    {"Restart required to activate this ECU protocol", "Reinicie para activar este protocolo ECU", "Reinicie para ativar este protocolo ECU", "ECUプロトコルの有効化には再起動が必要です"},
    {"RESET THEME LAYOUT", "RESTABLECER DISEÑO DEL TEMA", "REDEFINIR LAYOUT DO TEMA", "テーマ配置をリセット"},
    {"Choose a built-in theme to restore its default gauge assignments.", "Elija un tema integrado para restaurar sus indicadores.", "Escolha um tema integrado para restaurar os medidores.", "標準テーマを選び、メーター設定を初期化します。"},
    {"QUICK BRIGHTNESS", "BRILLO RÁPIDO", "BRILHO RÁPIDO", "クイック明るさ"},
    {"Day", "Día", "Dia", "昼"},
    {"Dim", "Tenue", "Suave", "薄暗い"},
    {"Night", "Noche", "Noite", "夜"},
    {"GETTING STARTED", "PRIMEROS PASOS", "PRIMEIROS PASSOS", "使い始める"},
    {"DASHBOARD CONTROLS", "CONTROLES DEL TABLERO", "CONTROLES DO PAINEL", "ダッシュボード操作"},
    {"THEMES AND DISPLAY", "TEMAS Y PANTALLA", "TEMAS E TELA", "テーマと表示"},
    {"LOGS", "REGISTROS", "REGISTROS", "ログ"},
    {"ECU, WARNINGS, AND DIAGNOSTICS", "ECU, ALERTAS Y DIAGNÓSTICO", "ECU, ALERTAS E DIAGNÓSTICOS", "ECU、警告、診断"},
    {"UPDATES, STORAGE, AND SAFETY", "ACTUALIZACIONES, ALMACENAMIENTO Y SEGURIDAD", "ATUALIZAÇÕES, ARMAZENAMENTO E SEGURANÇA", "更新、ストレージ、安全"},
    {"Odometer Calibration", "Calibración del odómetro", "Calibração do hodômetro", "走行距離の補正"},
    {"Trip A", "Viaje A", "Viagem A", "トリップ A"},
    {"Trip B", "Viaje B", "Viagem B", "トリップ B"},
    {"Enabled", "Activado", "Ativado", "有効"},
    {"Disabled", "Desactivado", "Desativado", "無効"},
    {"On", "Activado", "Ativado", "オン"},
    {"MPH", "MPH", "MPH", "MPH"},
    {"KPH", "KM/H", "KM/H", "KM/H"},
    {"VTEC", "VTEC", "VTEC", "VTEC"},
    {"ODO", "ODO", "HODÔMETRO", "走行距離"},
    {"TRIP A", "VIAJE A", "VIAGEM A", "トリップ A"},
    {"TRIP B", "VIAJE B", "VIAGEM B", "トリップ B"},
    {"Race LCD", "LCD de carrera", "LCD de corrida", "レース LCD"},
    {"Built in", "Integrado", "Integrado", "標準"},
    {"FIRMWARE UPDATE [DBG0721]", "ACTUALIZACIÓN DE FIRMWARE", "ATUALIZAÇÃO DE FIRMWARE", "ファームウェア更新"},
    {"Report a Problem", "Informar un problema", "Relatar um problema", "問題を報告"},
    {"Email", "Correo", "E-mail", "メール"},
    {"Leave Feedback / Review", "Dejar comentarios / reseña", "Enviar feedback / avaliação", "フィードバック / レビュー"},
    {"Trip Meters  ·  tap ODO on the dashboard to cycle", "Viajes  ·  toque ODO para cambiar", "Viagens  ·  toque em ODO para alternar", "トリップ  ·  ODOをタップして切り替え"},
    {"Reset all dashboard settings, warning limits, units, ECU selection, and theme layouts? This cannot be undone.", "¿Restablecer todos los ajustes, alertas, unidades, ECU y temas? Esto no se puede deshacer.", "Redefinir todas as configurações, alertas, unidades, ECU e temas? Isto não pode ser desfeito.", "すべての設定、警告、単位、ECU、テーマ配置をリセットしますか？この操作は元に戻せません。"},
    {"ECU DATA STALE", "DATOS DE ECU ANTIGUOS", "DADOS DA ECU DESATUALIZADOS", "ECU データ遅延"},
    {"REC", "GRAB", "GRAV", "記録"},
    {"SIM", "SIM", "SIM", "シミュ"},
    {"ACHIEVEMENT UNLOCKED", "LOGRO DESBLOQUEADO", "CONQUISTA DESBLOQUEADA", "実績を解除"},
    {"LOGGING STARTED", "GRABACIÓN INICIADA", "GRAVAÇÃO INICIADA", "記録を開始"},
    {"LOGGING STOPPED", "GRABACIÓN DETENIDA", "GRAVAÇÃO ENCERRADA", "記録を停止"},
    {"LOGGING FAILED", "ERROR DE GRABACIÓN", "FALHA NA GRAVAÇÃO", "記録エラー"},
    {"STOP FAILED", "ERROR AL DETENER", "FALHA AO PARAR", "停止エラー"},
    {"SD card not found", "Tarjeta SD no encontrada", "Cartão SD não encontrado", "SDカードが見つかりません"},
    {"Idle", "Ralentí", "Lenta", "アイドル"},
    {"Cruise", "Crucero", "Cruzeiro", "クルーズ"},
    {"Full Throttle", "Aceleración plena", "Aceleração total", "フルスロットル"},
    {"Redline", "Línea roja", "Corte", "レッドライン"},
    {"850 RPM  /  Neutral", "850 RPM  /  Neutro", "850 RPM  /  Neutro", "850 RPM  /  ニュートラル"},
    {"6500 RPM  /  Boost", "6500 RPM  /  Turbo", "6500 RPM  /  Pressão", "6500 RPM  /  ブースト"},
    {"8550 RPM  /  Limiter", "8550 RPM  /  Limitador", "8550 RPM  /  Limitador", "8550 RPM  /  リミッター"},
    {"CAN BUS", "BUS CAN", "BARRAMENTO CAN", "CAN バス"},
    {"PROTOCOL", "PROTOCOLO", "PROTOCOLO", "プロトコル"},
    {"LAST FRAME", "ÚLTIMA TRAMA", "ÚLTIMO QUADRO", "最終フレーム"},
    {"SIGNALS", "SEÑALES", "SINAIS", "信号"},
    {"ERRORS", "ERRORES", "ERROS", "エラー"},
    {"LVGL HEAP", "MEMORIA LVGL", "MEMÓRIA LVGL", "LVGL ヒープ"},
    {"MEMORY", "MEMORIA", "MEMÓRIA", "メモリ"},
    {"TASK STACK MARGINS", "MARGEN DE TAREAS", "MARGEM DAS TAREFAS", "タスクスタック余裕"},
    {"MAX RPM", "RPM MÁX.", "RPM MÁX.", "最大 RPM"},
    {"MAX SPEED", "VELOCIDAD MÁX.", "VELOCIDADE MÁX.", "最高速度"},
    {"MAX BOOST / MAP", "PRESIÓN / MAP MÁX.", "PRESSÃO / MAP MÁX.", "最大ブースト / MAP"},
    {"MAX COOLANT", "REFRIGERANTE MÁX.", "TEMP. MOTOR MÁX.", "最高冷却水温"},
    {"MAX INTAKE TEMP", "ADMISIÓN MÁX.", "TEMP. AR MÁX.", "最高吸気温"},
    {"MAX INJECTOR DUTY", "INYECTOR MÁX.", "INJETOR MÁX.", "最大インジェクター開度"},
    {"MAX KNOCK", "DETONACIÓN MÁX.", "DETONAÇÃO MÁX.", "最大ノック"},
    {"MIN AFR", "AFR MÍN.", "AFR MÍN.", "最小 AFR"},
    {"MIN OIL PRESSURE", "PRESIÓN DE ACEITE MÍN.", "PRESSÃO DO ÓLEO MÍN.", "最低油圧"},
    {"MIN BATTERY", "BATERÍA MÍN.", "BATERIA MÍN.", "最低電圧"},
    {"General (LOG)\nDyno\nTune\nRace\nTest", "General (LOG)\nBanco\nAjuste\nCarrera\nPrueba", "Geral (LOG)\nDinamômetro\nAcerto\nCorrida\nTeste", "一般 (LOG)\nダイノ\nチューニング\nレース\nテスト"},
    {"Green\nYellow\nAmber\nRed\nBlue\nCyan\nWhite\nMagenta", "Verde\nAmarillo\nÁmbar\nRojo\nAzul\nCian\nBlanco\nMagenta", "Verde\nAmarelo\nÂmbar\nVermelho\nAzul\nCiano\nBranco\nMagenta", "緑\n黄\nアンバー\n赤\n青\nシアン\n白\nマゼンタ"},
    {"RPM Bar Shift Colors", "Colores de cambio de RPM", "Cores de troca da barra RPM", "RPMバーのシフト色"},
    {"Stage 1 Start", "Inicio etapa 1", "Início estágio 1", "ステージ1 開始"},
    {"Stage 2 Start", "Inicio etapa 2", "Início estágio 2", "ステージ2 開始"},
    {"Stage 3 Start", "Inicio etapa 3", "Início estágio 3", "ステージ3 開始"},
    {"Shift Color Brightness", "Brillo de luces de cambio", "Brilho das luzes de troca", "シフトライトの明るさ"},
    {"Flash All at Redline", "Parpadear todo en línea roja", "Piscar tudo no corte", "レッドラインですべて点滅"},
    {"VTEC & REDLINE", "VTEC Y LÍNEA ROJA", "VTEC E CORTE", "VTEC とレッドライン"},
    {"SHIFT LIGHTS", "LUCES DE CAMBIO", "LUZES DE TROCA", "シフトライト"},
    {"HOLD SETTINGS", "MANTENER AJUSTES", "SEGURE CONFIGURAÇÕES", "設定を長押し"},
    {"Open quick Day, Dim, and Night brightness modes.", "Abre los modos rápidos Día, Tenue y Noche.", "Abre os modos rápidos Dia, Suave e Noite.", "昼、薄暗い、夜の明るさを選択します。"},
    {"HOLD A DATA VALUE", "MANTENER UN VALOR", "SEGURE UM VALOR", "データ値を長押し"},
    {"Choose which channel that value shows on built-in themes.", "Elige qué canal muestra ese valor en los temas integrados.", "Escolha qual canal será mostrado nos temas integrados.", "標準テーマで表示するチャンネルを選びます。"},
    {"TAP / HOLD SIM", "TOCAR / MANTENER SIM", "TOQUE / SEGURE SIM", "SIMをタップ / 長押し"},
    {"Tap to toggle simulation. Hold to choose an RPM and driving mode.", "Toque para alternar la simulación. Mantenga para elegir RPM y modo.", "Toque para alternar a simulação. Segure para escolher RPM e modo.", "タップで切り替え、長押しでRPMと走行モードを選びます。"},
    {"TAP REC", "TOCAR GRAB", "TOQUE GRAV", "RECをタップ"},
    {"Start or stop a driving log on the SD card.", "Inicia o detiene un registro en la tarjeta SD.", "Inicia ou para um registro no cartão SD.", "SDカードへの走行記録を開始または停止します。"},
    {"TAP ODO", "TOCAR ODO", "TOQUE ODO", "ODOをタップ"},
    {"On MackoDash, cycle between odometer, Trip A, and Trip B.", "Alterna entre odómetro, Viaje A y Viaje B.", "Alterna entre hodômetro, Viagem A e Viagem B.", "走行距離、トリップA、トリップBを切り替えます。"},
    {"HOLD ODO", "MANTENER ODO", "SEGURE ODO", "ODOを長押し"},
    {"Reset the displayed Trip A or Trip B after confirmation.", "Restablece el Viaje A o B después de confirmar.", "Redefine a Viagem A ou B após confirmar.", "確認後、表示中のトリップAまたはBをリセットします。"},
    {"TOUCH A LOG GRAPH", "TOCAR UN GRÁFICO", "TOQUE NO GRÁFICO", "ロググラフをタッチ"},
    {"Show the exact recorded values at that point in the drive.", "Muestra los valores exactos registrados en ese punto.", "Mostra os valores exatos registrados naquele ponto.", "その地点の正確な記録値を表示します。"},
    {"TAP A THEME", "TOCAR UN TEMA", "TOQUE EM UM TEMA", "テーマをタップ"},
    {"Stage your selection. Press Set to apply it and return to the dashboard.", "Prepare la selección. Pulse Aplicar para volver al tablero.", "Prepare a seleção. Pressione Aplicar para voltar ao painel.", "選択後、適用を押してダッシュボードに戻ります。"},
    {"Driving Logs", "Registros de conducción", "Registros de condução", "走行ログ"},
    {"Session Peaks", "Máximos de sesión", "Picos da sessão", "セッション最大値"},
    {"Achievements", "Logros", "Conquistas", "実績"},
    {"Odometer & Trip Settings", "Ajustes de odómetro y viajes", "Hodômetro e viagens", "走行距離とトリップ設定"},
    {"1. Open Settings > ECU and select the correct protocol, or use Auto.\n"
     "2. Open Display > Units and choose speed, temperature, pressure, and distance units.\n"
     "3. Set VTEC, redline, shift lights, and warning limits under Engine Limits.\n"
     "4. Use SIM while parked to verify the selected theme and warning behavior.\n"
     "5. Start the engine while stationary and compare every value with a trusted source.",
     "1. Abra Ajustes > ECU y elija el protocolo correcto o Auto.\n"
     "2. Abra Pantalla > Unidades y configure las unidades.\n"
     "3. Configure VTEC, línea roja, luces de cambio y alertas.\n"
     "4. Use SIM con el vehículo estacionado para verificar el tema.\n"
     "5. Compare los valores con una fuente confiable.",
     "1. Abra Configurações > ECU e escolha o protocolo correto ou Auto.\n"
     "2. Abra Tela > Unidades e configure as unidades.\n"
     "3. Configure VTEC, corte, luzes de troca e alertas.\n"
     "4. Use SIM com o veículo parado para verificar o tema.\n"
     "5. Compare os valores com uma fonte confiável.",
     "1. 設定 > ECUで正しいプロトコルまたは自動を選びます。\n"
     "2. ディスプレイ > 単位で各単位を設定します。\n"
     "3. VTEC、レッドライン、シフトライト、警告を設定します。\n"
     "4. 停車中にSIMでテーマと警告を確認します。\n"
     "5. エンジン始動後、信頼できる計器と値を比較します。"},
    {"Settings opens configuration. Hold Settings for Day, Dim, and Night brightness. "
     "Tap SIM to start or stop simulation; hold SIM to choose an RPM and driving mode. "
     "Tap REC to start or stop an SD-card driving log. Tap ODO to cycle Odometer, Trip A, "
     "and Trip B. Hold a displayed trip to reset it after confirmation. Hold a configurable "
     "data value to choose the channel shown in that position.",
     "Ajustes abre la configuración. Mantenga Ajustes para elegir brillo Día, Tenue o Noche. Toque SIM para alternar la simulación y manténgalo para elegir RPM y modo. Toque GRAB para el registro SD. Toque ODO para cambiar entre odómetro y viajes. Mantenga un valor para elegir su canal.",
     "Configurações abre os ajustes. Segure para escolher o brilho Dia, Suave ou Noite. Toque SIM para alternar a simulação e segure para escolher RPM e modo. Toque GRAV para registrar no SD. Toque ODO para alternar as viagens. Segure um valor para escolher o canal.",
     "設定で構成を開きます。長押しで昼、薄暗い、夜の明るさを選びます。SIMはタップで切り替え、長押しでRPMと走行モードを選びます。RECでSD記録、ODOで走行距離とトリップを切り替えます。値を長押しするとチャンネルを選べます。"},
    {"Choose a built-in or SD-card theme under Theme, then press Set. Only the selected theme "
     "updates and renders. Display contains brightness, RPM/speed smoothing, simulation-button "
     "visibility, and Units. Smoothing levels are Off, Light, Medium, and Strong. Theme Resets "
     "restores the default channel assignments for one built-in theme.",
     "Elija un tema integrado o de la tarjeta SD y pulse Aplicar. Pantalla contiene brillo, suavizado, botón SIM y unidades. Restablecer tema recupera los canales predeterminados.",
     "Escolha um tema integrado ou do cartão SD e pressione Aplicar. Tela contém brilho, suavização, botão SIM e unidades. Redefinir tema restaura os canais padrão.",
     "標準またはSDカードのテーマを選び、適用を押します。ディスプレイでは明るさ、スムージング、SIMボタン、単位を設定できます。テーマのリセットで標準チャンネルに戻せます。"},
    {"Driving Logs records ECU data to the SD card and lets you inspect saved graphs. Auto Record "
     "starts a log when live engine data meets the recording conditions. Session Peaks shows the "
     "highest or lowest values from the current session. Achievements tracks threshold events and "
     "personal records. The red REC badge shows the active filename and elapsed recording time.",
     "Registros guarda datos de la ECU en la tarjeta SD. Grabación automática comienza al cumplirse las condiciones. Máximos de sesión muestra los extremos y Logros registra eventos y récords. El indicador rojo muestra el archivo y tiempo activo.",
     "Registros salva dados da ECU no cartão SD. Gravação automática inicia quando as condições são atendidas. Picos da sessão mostra os extremos e Conquistas registra eventos e recordes. O indicador vermelho mostra o arquivo e tempo ativo.",
     "走行ログはECUデータをSDカードに保存します。自動記録は条件を満たすと開始します。セッション最大値は最大・最小値を、実績はイベントと記録を表示します。赤いRECにファイル名と経過時間が表示されます。"},
    {"The ECU page selects Auto, Hondata, Haltech, Link G4X, MegaSquirt, Emtron, MaxxECU, or "
     "ECUMaster Black. Available channels depend on the ECU broadcast. The centered amber label "
     "means ECU data is disconnected or stale and disappears when live data returns. Engine Limits "
     "controls shift-light stages, VTEC, redline, and warning thresholds. Diagnostics reports CAN "
     "state, frame age, errors, memory, task stack margins, reset reason, and actual display FPS.",
     "ECU selecciona el protocolo. Los canales dependen de la transmisión de la ECU. La etiqueta ámbar indica datos desconectados o antiguos. Límites del motor controla luces, VTEC, línea roja y alertas. Diagnóstico muestra CAN, errores, memoria y FPS.",
     "ECU seleciona o protocolo. Os canais dependem da transmissão da ECU. A etiqueta âmbar indica dados desconectados ou antigos. Limites do motor controla luzes, VTEC, corte e alertas. Diagnósticos mostra CAN, erros, memória e FPS.",
     "ECUページでプロトコルを選択します。使用可能なチャンネルはECU送信内容によります。中央のアンバー表示はECU未接続またはデータ遅延を示します。エンジン制限でシフトライト、VTEC、レッドライン、警告を設定し、診断でCAN、エラー、メモリ、FPSを確認できます。"},
    {"Use the official MackoDash Utility on Windows for firmware updates. Keep dashboard power and "
     "USB connected until verification finishes. Custom .mdtheme.zip packages belong in "
     "/MACKODASH/THEMES on a FAT-formatted microSD card. Configure and test while stationary. "
     "Verify CAN scaling, units, thresholds, wiring, and displayed values against trusted equipment. "
     "Do not use MackoDash as a replacement for required factory safety systems.",
     "Use MackoDash Utility en Windows para actualizar. Mantenga la alimentación y USB conectados hasta finalizar. Guarde los temas en /MACKODASH/THEMES de una microSD FAT. Configure con el vehículo detenido y verifique todos los valores. MackoDash no reemplaza los sistemas de seguridad originales.",
     "Use o MackoDash Utility no Windows para atualizar. Mantenha a alimentação e USB conectados até terminar. Salve os temas em /MACKODASH/THEMES de um microSD FAT. Configure com o veículo parado e verifique todos os valores. MackoDash não substitui os sistemas de segurança originais.",
     "更新にはWindows版MackoDash Utilityを使用し、完了まで電源とUSBを接続してください。テーマはFAT形式microSDの/MACKODASH/THEMESに保存します。停車中に設定し、値を信頼できる計器で確認してください。MackoDashは純正安全装置の代替ではありません。"},
};

const char *dashboard_i18n_translate(const char *english)
{
    if (!english || !english[0]) return english;
    dash_config_language_t language = dash_config_get_language();
    if (language == DASH_CONFIG_LANGUAGE_ENGLISH) return english;

    for (size_t index = 0; index < sizeof(TRANSLATIONS) / sizeof(TRANSLATIONS[0]); ++index) {
        if (strcmp(TRANSLATIONS[index].english, english) != 0) continue;
        switch (language) {
            case DASH_CONFIG_LANGUAGE_SPANISH: return TRANSLATIONS[index].spanish;
            case DASH_CONFIG_LANGUAGE_PORTUGUESE: return TRANSLATIONS[index].portuguese;
            case DASH_CONFIG_LANGUAGE_JAPANESE: return TRANSLATIONS[index].japanese;
            default: return english;
        }
    }
    return english;
}

const char *dashboard_i18n_language_options(void)
{
    return "English\nEspañol\nPortuguês\n日本語";
}