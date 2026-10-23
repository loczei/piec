const oxygen_ctx = document.getElementById('tlen');

let oxygen = new Chart(oxygen_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Poziom tlenu [%]',
            data: [],
            fill: false,
        },
        {
            label: 'Zadany tlen [%]',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: false,
        scales: {
            y: {
                min: 0.0,
                max: 23.0
            }
        }
    }
});

const servo_ctx = document.getElementById('serwo');

let servo = new Chart(servo_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [
        {
            label: 'Wychylenie serwa [%]',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: false,
        scales: {
            y: {
                min: 0.0,
                max: 100.0
            }
        }
    }
});

let temp_ip = localStorage.getItem("ip");

if(temp_ip) {
    document.querySelector("#ip").value = temp_ip;
}

document.querySelector("#scale").addEventListener("change", () => {
    if (document.querySelector("#scale").checked) {
        oxygen.options.scales.y.min = 0.0;
        oxygen.options.scales.y.max = 23.0;
    } else {
        oxygen.options.scales.y.min = null;
        oxygen.options.scales.y.max = null;
    }
});

let history = 200;

function updateHistory(newHistory) {
    history = newHistory;

    remove = oxygen.data.datasets[0].len - history;

    if (remove <= 0) return;

    oxygen.data.labels.splice(0, remove);
    oxygen.data.datasets[0].data.splice(0, remove);
    oxygen.data.datasets[1].data.splice(0, remove);

    servo.data.labels.splice(0, remove);
    servo.data.datasets[0].data.splice(0, remove);
}

document.querySelector("#history-button").addEventListener("click", () => {
    updateHistory(Number(document.querySelector("#history").value));
});

let socket;

const ipButton = document.querySelector('#ip-button');

let updateFuncs = [];

function updateInfo(msg) {
    const percent = Number.parseFloat((msg.servo - msg["Min Servo"]) / (msg["Max Servo"] - msg["Min Servo"]) * 100)
    const servo = Number.parseFloat(msg.servo)
    const alpha = Number.parseFloat(msg.alpha)
    const beta = Number.parseFloat(msg.beta)

    document.querySelector(".info-oxygen").innerHTML = `Odczyt: ${msg.oxygen} Zadana: ${msg["Target Oxygen"]}`
    document.querySelector(".info-par").innerHTML =
    `Wychylenie serwa: ${percent.toFixed(2)}% Wychylenie serwa: ${servo.toFixed(2)}  <br> Alpha: ${alpha.toFixed(2)} Beta: ${beta.toFixed(2)}`
}

ipButton.addEventListener("click", () => {
    console.log("Connecting");
    let ip = document.querySelector("#ip").value;
    socket = new WebSocket(`ws://${ip}`);
    ipButton.textContent = "Łączenie";

    socket.addEventListener("open", () => {
        ipButton.textContent = "Połączono";
        ipButton.active = false;
        ipButton.style.backgroundColor = "green";

        localStorage.setItem("ip", ip);
    });

    socket.addEventListener("message", (e) => {
        console.log(e.data);
        let msg = JSON.parse(e.data);

        let totalSeconds = Math.floor(msg.time / 1000);

        let millis = String(msg.time % 1000).padStart(3, '0');
        let seconds = String(totalSeconds % 60).padStart(2, '0');
        let minutes = String(Math.floor(totalSeconds / 60)).padStart(2, '0');

        let time = `${minutes}:${seconds}.${millis}`;
        
        oxygen.data.labels.push(time);
        oxygen.data.datasets[0].data.push(msg.oxygen);
        oxygen.data.datasets[1].data.push(msg["Target Oxygen"]);

        oxygen.update();

        servo.data.labels.push(time);
        servo.data.datasets[0].data.push((msg.servo - msg["Min Servo"]) / (msg["Max Servo"] - msg["Min Servo"]) * 100);

        if (history < oxygen.data.datasets[0].len) {
            oxygen.data.labels.shift();
            oxygen.data.datasets[0].data.shift();
            oxygen.data.datasets[1].data.shift();

            servo.data.labels.shift();
            servo.data.datasets[0].data.shift();
        }

        servo.update();

        updateInfo(msg);
        updateFuncs.forEach((e) => e(msg));
    });

    socket.addEventListener("close", () => {
        ipButton.textContent = "Połącz";
        ipButton.active = true;
        ipButton.style.backgroundColor = "#006381";
    });
    socket.addEventListener("error", () => {
        ipButton.textContent = "Połącz";
        ipButton.active = true;
        ipButton.style.backgroundColor = "#006381";
    });
});

document.querySelectorAll(".history-button").forEach((e) => e.addEventListener("click", (ev) => {
    updateHistory(Number(ev.currentTarget.innerHTML));
}))

function button(name, char, payload, oname, odesc, pname) {
    if (oname !== undefined) {
        document.querySelector(".options").insertAdjacentHTML("beforeend", `
            <div class="option">
                <div class="option-name option-${name}">${oname} (brak połączenia): </div>
                <div class="option-desc">${odesc}</div>
                <div class="option-input"><input type="text" id="${name}"><button id="${name}-button">Ustaw</button></div>
            </div>
        `);

        updateFuncs.push((msg) => { 
            document.querySelector(`.option-${name}`).innerHTML = `${oname} (${msg !== undefined && msg[pname] !== undefined ? msg[pname] : "błąd1"}):`;
        })
    }
    
    document.querySelector(`#${name}-button`).addEventListener("click", () => {
        if (socket) {
            let str;
            if (payload) {
                str = `${char} ${document.querySelector(`#${name}`).value}`;
            } else {
                str = `${char}`;
            }
            
            socket.send(str);
        }

        // console.log("TEst" + pname);
        // updateFuncs.forEach((e) => e());
    });
}

const buttons = [
    {
        "name": "zadana",
        "char": "TO",
        "payload": true,
    },
    {
        "name": "deadzone",
        "char": "DZ",
        "payload": true,
        "fullname": "Martwa strefa O₂ [%]",
        "description": "Zakres wokół wartości zadanej, w którym serwo nie wykonuje żadnych ruchów. Wartości 0.00-25.00",
        "arduinoname": "Dead Zone",
    },
    {
        "name": "overdrive",
        "char": "Ov",
        "payload": true,
        "fullname": "Próg szybkiej korekty O₂ [%]",
        "description": "Po przekroczeniu określonej różnicy pomiędzy O₂ zmierzonym a zadanym regulator ma reagować szybciej. Wartości 0.00-25.00",
        "arduinoname": "Overdrive",
    },
    {
        "name": "maxServo",
        "char": "MS",
        "payload": true,
        "fullname": "Maksymalne położenie klapki PW",
        "description": "Regulowane ograniczenie, którego serwo nie może przekroczyć w kierunku pełnego otwarcia. Wartości: 0-4096",
        "arduinoname": "Max Servo",
    },
    {
        "name": "minServo",
        "char": "Mi",
        "payload": true,
        "fullname": "Minimalne położenie klapki PW",
        "description": "Regulowane ograniczenie, którego serwo nie może przekroczyć w kierunku zamknięcia. Wartości: 0-4096",
        "arduinoname": "Min Servo",
    },
    {
        "name": "openStep",
        "char": "OS",
        "payload": true,
        "fullname": "Wielkość kroku OTWIERANIA klapki PW",
        "description": "Wartości: 0-4096",
        "arduinoname": "Open Step",
    },
    {
        "name": "closeStep",
        "char": "CS",
        "payload": true,
        "fullname": "Wielkość kroku ZAMYKANIA klapki PW",
        "description": "Wartości: 0-4096",
        "arduinoname": "Close Step",
    },
    {
        "name": "waitTime",
        "char": "WT",
        "payload": true,
        "fullname": "Czas oczekiwania po wykonaniu kroku [ms]",
        "description": "Po każdym ruchu serwo stoi przez ustawiony czas. Dopiero później wykonywana jest kolejna ocena O₂. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "Wait Time",
    },
    {
        "name": "startPosition",
        "char": "SP",
        "payload": true,
        "fullname": "Pozycja startowa klapki PW",
        "description": "Pozycja klapki podczas uruchamiania procesu. Wartości 0-4096",
        "arduinoname": "Start Position",
    },
    {
        "name": "initialWaitTime",
        "char": "IW",
        "payload": true,
        "fullname": "Opóźnienie rozpoczęcia automatycznej regulacji O₂ [ms]",
        "description": "Po uruchomieniu klapka pozostaje w pozycji startowej przez ustawiony czas. Dopiero później rozpoczyna się regulacja na podstawie O₂. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "Initial Wait Time",
    },
    {
        "name": "checkTime",
        "char": "CT",
        "payload": true,
        "fullname": "Czas oczekiwania szybkiej korekty [ms]",
        "description": "Możliwość ustawienia krótszego czasu oczekiwania niż podczas normalnej regulacji.. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "Check Time",
    },
    {
        "name": "overdriveStep",
        "char": "VS",
        "payload": true,
        "fullname": "Wielkość kroku szybkiej korekty",
        "description": "Osobny, większy krok stosowany przy dużym błędzie O₂. Wartości 0-4096",
        "arduinoname": "Overdrive Step",
    },
    {
        "name": "oxygenPumpCutOut",
        "char": "OP",
        "payload": true,
        "fullname": "Próg odcięcia zasilania pompy [%]",
        "description": "Wartości 0.00-25.00",
        "arduinoname": "Oxygen Pump Cut Out",
    },
{
    "name": "raiseMulti",
    "char": "RM",
    "payload": true,
    "fullname": "Mnożnik prędkości serwa przy wykryciu zwiększania różnicy [-]",
    "description": "Wartości 0.00+",
    "arduinoname": "Raise Multi",
},
{
    "name": "fallMulti",
    "char": "FM",
    "payload": true,
    "fullname": "Mnożnik prędkości serwa przy wykryciu zmniejszania różnicy [-]",
    "description": "Wartości 0.00+",
    "arduinoname": "Fall Multi",
},
{
    "name": "neutralMulti",
    "char": "NM",
    "payload": true,
    "fullname": "Mnożnik prędkości serwa przy wykryciu braku zmiany różnicy [-]",
    "description": "Wartości 0.00+",
    "arduinoname": "Neutral Multi",
},
{
    "name": "alphaThreshold",
    "char": "AT",
    "payload": true,
    "fullname": "Próg wartości alpha decydującego czy mamy doczynienia ze znaczym zwiększeniem/zmniejszeniem różnicy [%]",
    "description": "Wartości 0.00+",
    "arduinoname": "Alpha Threshold",
},
{
    "name": "dataAcquire",
    "char": "DA",
    "payload": true,
    "fullname": "Co jaki czas mają być zbierana dane do regresji [ms]",
    "description": "Wartości 0+",
    "arduinoname": "Data Acquire",
},
];

buttons.forEach((b) => {
    button(b.name, b.char, b.payload, b.fullname, b.description, b.arduinoname)
})
