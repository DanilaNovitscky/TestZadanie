Client-Server application for monitoring work activity

Explanation
Simple application to show current work activity of all employers in organisation

Example applications 
https://www.teramind.co/solutions/employee-monitoring-software
https://veriato.com/product/

Client (windows) - c/c++
Silent launches on user logon and work in background
Communicates with server at any protocol
You can't use third-party libraries like boost and others, and you can't use frameworks like Qt and others. 

Server - desktop or web interface - any language 
List all connected clients - domain/machine/ip/user
Show client’s last active time
Ability to get screenshot from client’s desktop 

In response send link to github.com project page, which contains all Visual Studio solution files with full source code and dependencies if any.


Раздел server содержит папку с кодом сервера, работает на node.js

Раздел c++code содержит папку с программой на с++

Файлы server скачиваем в одну папку и запускаем сервер на node.js

Папку с++ скачиваем, открываем в visualstudio

Реализованно:

             Подключение агента к серверу( имя агента определяется в коде программы, имя пк + ip(на стороне сервера)
						 
             Старт таймера при первом подключении агента( стоп таймера при его отключении и пуск при подключении)
						 
             Кнопка скриншот(делает скриншот рабочего стола агента, отображает его ниже имени агента, можно скачать, открыть в новом окне и т.д.)
						 
             Логирование действий(при отключении создается/обновляется файл хранящий информацию об имени пользователя и общем времени его подключения.Так же при скриншоте картинка сохраняется в папку с кодом сервера)
            
	     Первый ручной запуск программы добавляет её в автозагрузку.

      	     Программа рабоатет в фоновом режиме( видна только через диспетчер задач)
             
