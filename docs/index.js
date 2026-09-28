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

document.querySelector("#history-button").addEventListener("click", () => {
    history = Number(document.querySelector("#history").value);
});

let socket;

const ipButton = document.querySelector('#ip-button');

let updateFuncs = [];

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
        
        oxygen.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        oxygen.data.datasets[0].data.push(msg.oxygen);
        oxygen.data.datasets[1].data.push(msg.target);

        oxygen.update();

        servo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        servo.data.datasets[0].data.push((msg.servo - msg.minServo) / (msg.maxServo - msg.minServo) * 100);

        if (history < oxygen.data.datasets[0].len) {
            oxygen.data.labels.shift();
            oxygen.data.datasets[0].data.shift();
            oxygen.data.datasets[1].data.shift();

            servo.data.labels.shift();
            servo.data.datasets[0].data.shift();
        }

        servo.update();

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
            if (msg[pname] !== undefined) document.querySelector(`#${name}`).value = msg[pname];
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
        "char": "T",
        "payload": true,
    },
    {
        "name": "deadzone",
        "char": "d",
        "payload": true,
        "fullname": "Martwa strefa O₂ [%]",
        "description": "Zakres wokół wartości zadanej, w którym serwo nie wykonuje żadnych ruchów. Wartości 0.00-25.00",
        "arduinoname": "deadZone",
    },
    {
        "name": "overdrive",
        "char": "o",
        "payload": true,
        "fullname": "Próg szybkiej korekty O₂ [%]",
        "description": "Po przekroczeniu określonej różnicy pomiędzy O₂ zmierzonym a zadanym regulator ma reagować szybciej. Wartości 0.00-25.00",
        "arduinoname": "overdrive",
    },
    {
        "name": "maxServo",
        "char": "S",
        "payload": true,
        "fullname": "Maksymalne położenie klapki PW",
        "description": "Regulowane ograniczenie, którego serwo nie może przekroczyć w kierunku pełnego otwarcia. Wartości: 0-4096",
        "arduinoname": "maxServo",
    },
    {
        "name": "minServo",
        "char": "s",
        "payload": true,
        "fullname": "Minimalne położenie klapki PW",
        "description": "Regulowane ograniczenie, którego serwo nie może przekroczyć w kierunku zamknięcia. Wartości: 0-4096",
        "arduinoname": "minServo",
    },
    {
        "name": "openStep",
        "char": "t",
        "payload": true,
        "fullname": "Wielkość kroku OTWIERANIA klapki PW",
        "description": "Wartości: 0-4096",
        "arduinoname": "openStep",
    },
    {
        "name": "closeStep",
        "char": "c",
        "payload": true,
        "fullname": "Wielkość kroku ZAMYKANIA klapki PW",
        "description": "Wartości: 0-4096",
        "arduinoname": "closeStep",
    },
    {
        "name": "waitTime",
        "char": "w",
        "payload": true,
        "fullname": "Czas oczekiwania po wykonaniu kroku [ms]",
        "description": "Po każdym ruchu serwo stoi przez ustawiony czas. Dopiero później wykonywana jest kolejna ocena O₂. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "waitTime",
    },
    {
        "name": "startPosition",
        "char": "p",
        "payload": true,
        "fullname": "Pozycja startowa klapki PW",
        "description": "Pozycja klapki podczas uruchamiania procesu. Wartości 0-4096",
        "arduinoname": "startPosition",
    },
    {
        "name": "initialWaitTime",
        "char": "i",
        "payload": true,
        "fullname": "Opóźnienie rozpoczęcia automatycznej regulacji O₂ [ms]",
        "description": "Po uruchomieniu klapka pozostaje w pozycji startowej przez ustawiony czas. Dopiero później rozpoczyna się regulacja na podstawie O₂. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "initialWaitTime",
    },
    {
        "name": "checkTime",
        "char": "e",
        "payload": true,
        "fullname": "Czas oczekiwania szybkiej korekty [ms]",
        "description": "Możliwość ustawienia krótszego czasu oczekiwania niż podczas normalnej regulacji.. Wartości: 0+ (W milisekundach tzn. ustawianie tu 1000 da 1 sekundę oczekiwania)",
        "arduinoname": "checkTime",
    },
    {
        "name": "overdriveStep",
        "char": "O",
        "payload": true,
        "fullname": "Wielkość kroku szybkiej korekty",
        "description": "Osobny, większy krok stosowany przy dużym błędzie O₂. Wartości 0-4096",
        "arduinoname": "overdriveStep",
    },
    {
        "name": "oxygenPumpCutOut",
        "char": "P",
        "payload": true,
        "fullname": "Próg odcięcia zasilania pompy [%]",
        "description": "Wartości 0.00-25.00",
        "arduinoname": "oxygenPumpCutOut",
    },
];

buttons.forEach((b) => {
    button(b.name, b.char, b.payload, b.fullname, b.description, b.arduinoname)
})
