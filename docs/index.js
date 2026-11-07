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
                max: 25.0
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

document.querySelector("#button-config").addEventListener("click", () => {
    document.querySelector(".config").style.display = "flex";
    document.querySelector(".panel").style.display = "none";
});
document.querySelector("#button-panel").addEventListener("click", () => {
    document.querySelector(".config").style.display = "none";
    document.querySelector(".panel").style.display = "flex";
});

let temp_ip = localStorage.getItem("ip");

if(temp_ip) {
    document.querySelector("#ip").value = temp_ip;
}

document.querySelector("#scale").addEventListener("change", () => {
    if (document.querySelector("#scale").checked) {
        oxygen.options.scales.y.min = 0.0;
        oxygen.options.scales.y.max = 25.0;
    } else {
        oxygen.options.scales.y.min = null;
        oxygen.options.scales.y.max = null;
    }
});

let history = 200;

function updateHistory(newHistory) {
    history = newHistory;

    const currentLength = oxygen.data.datasets[0].data.length;
    const remove = currentLength - history;

    if (remove <= 0) return;

    oxygen.data.labels.splice(0, remove);
    oxygen.data.datasets[0].data.splice(0, remove);
    if (oxygen.data.datasets[1]) {
        oxygen.data.datasets[1].data.splice(0, remove);
    }

    servo.data.labels.splice(0, remove);
    servo.data.datasets[0].data.splice(0, remove);

    oxygen.update();
    servo.update();
}



document.querySelector("#history-button").addEventListener("click", () => {
    updateHistory(Number(document.querySelector("#history").value));
});

let csvAcquire = false;
let csvInfo = document.querySelector(".status-csv");
let csvData = JSON.parse(localStorage.getItem("csv")) || [];

csvInfo.innerHTML = `Aktualnie nie są zbierane dane. Plik CSV posiada ${csvData.length} pozycji.`


document.querySelector("#button-start").addEventListener("click", () => {
    if (csvAcquire == false) {
        csvAcquire = true;
        csvInfo.innerHTML = `Aktualnie są zbierane dane. Plik CSV posiada ${csvData.length} pozycji.`;
        document.querySelector("#button-start").style.backgroundColor = "red";
        document.querySelector("#button-start").innerHTML = "Stop CSV";
    } else {
        csvAcquire = false;
        csvInfo.innerHTML = `Aktualnie nie są zbierane dane. Plik CSV posiada ${csvData.length} pozycji.`;
        document.querySelector("#button-start").style.backgroundColor = "#006381";
        document.querySelector("#button-start").innerHTML = "Start CSV";
    }

});

document.querySelector("#button-restart").addEventListener("click", () => {
    csvAcquire = false;
    csvData = [];
    localStorage.setItem("csv", JSON.stringify(csvData));
    csvInfo.innerHTML = `Aktualnie nie są zbierane dane. Plik CSV posiada ${csvData.length} pozycji.`;
    document.querySelector("#button-start").style.backgroundColor = "#006381";
    document.querySelector("#button-start").innerHTML = "Start CSV";
});

document.querySelector("#button-download").addEventListener("click", () => {
    const now = new Date();
    let filename = `pomiary ${now.toISOString()}.csv`;

    if (!csvData || csvData.length === 0) {
        alert("Brak danych do pobrania!");
        return;
    }

    // 1. Zdefiniowanie nagłówków
    const headers = "Czas;Tlen;Serwo;Zadana";

    // 2. Połączenie nagłówków z zebranymi danymi znakiem nowej linii
    const csvContent = headers + "\r\n" + csvData.join("\r\n");

    // 3. Dodanie znaku BOM (\uFEFF) - gwarantuje prawidłowe kodowanie znaków i separatorów w MS Excel
    const blob = new Blob(["\uFEFF" + csvContent], { type: "text/csv;charset=utf-8;" });

    // 4. Utworzenie tymczasowego linku i wymuszenie pobierania
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.setAttribute("href", url);
    link.setAttribute("download", filename);
    link.style.display = "none";

    document.body.appendChild(link);
    link.click();

    // 5. Posprzątanie po pobraniu
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
});

let socket;

const ipButton = document.querySelector('#ip-button');

let updateFuncs = [];

let minServo = 0;
let maxServo = 0;

function updateInfo(msg) {
    const percent = Number.parseFloat((msg.servo - minServo) / (msg["Max Servo"] - minServo) * 100)
    const servo = Number.parseFloat(msg.servo)

    const alpha = Number.parseFloat(msg.linA)
    const r2_lin = Number.parseFloat(msg.linR)

    const a = Number.parseFloat(msg.sqrA)
    const r2_sqr = Number.parseFloat(msg.sqrR)

    document.querySelector(".info-oxygen").innerHTML = `Odczyt: ${msg.oxygen} Zadana: ${msg["Target Oxygen"]}`
    document.querySelector(".info-par").innerHTML =
    `Wychylenie serwa: ${percent.toFixed(2)}% Wychylenie serwa: ${servo.toFixed(2)}
    <br> Regresja liniowa Alpha: ${alpha.toFixed(2)} R^2: ${r2_lin.toFixed(2)}
    <br> Regresja kwadratowa Alpha: ${a.toFixed(2)} R^2: ${r2_sqr.toFixed(2)}
    <br> Spoczynek: ${Number.parseFloat(msg.atRest) == 1 ? "Tak" : "Nie"}
    `
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

        if (history < oxygen.data.datasets[0].data.length) {
            oxygen.data.labels.shift();
            oxygen.data.datasets[0].data.shift();
            oxygen.data.datasets[1].data.shift();

            servo.data.labels.shift();
            servo.data.datasets[0].data.shift();
        }

        servo.update();

        if (csvAcquire) {
            csvData.push(`${msg.time};${msg.oxygen};${msg.servo};${msg["Target Oxygen"]}`);
            localStorage.setItem("csv", JSON.stringify(csvData));
            csvInfo.innerHTML = `Aktualnie są zbierane dane. Plik CSV posiada ${csvData.length} pozycji.`;    
        }
        
        updateInfo(msg);
        if (msg["Max Servo"]) {
            minServo = msg["Min Servo"];
            maxServo = msg["Max Servo"];
            updateFuncs.forEach((e) => e(msg));
        }
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
        "name": "oxygenPumpCutOut",
        "char": "OP",
        "payload": true,
        "fullname": "Próg odcięcia zasilania pompy [%]",
        "description": "Wartości 0.00-25.00",
        "arduinoname": "Oxygen Pump Cut Out",
    },
    {
        "name": "dataAcquire",
        "char": "DA",
        "payload": true,
        "fullname": "Co jaki czas mają być zbierana dane do regresji [ms]",
        "description": "Wartości 0+",
        "arduinoname": "Data Acquire",
    },
    {
        "name": "deadzone",
        "char": "DZ",
        "payload": true,
        "fullname": "Strefa martwa regulacji",
        "description": "Dopuszczalna odchyłka od tlenu zadanego, w której układ dąży do spoczynku. Wartości: 0.0 - 10.0",
        "arduinoname": "Dead Zone"
    },
    {
        "name": "fallSteep",
        "char": "FS",
        "payload": true,
        "fullname": "Próg szybkiego spadku",
        "description": "Wartość oceniająca, czy tlen nie zbliża się za szybko do zadanego (Zmniejszenie tej wartości sprawi, że algorytm będzie próbował hamować zmianę tlenu). Wartości: 0.0 - 100.0",
        "arduinoname": "Fall Steep"
    },
    {
        "name": "bypass",
        "char": "BP",
        "payload": true,
        "fullname": "Wybudzenie przy wykryciu gwałtownej zmiany tlenu",
        "description": "Ustawione na 1 powoduje, że algorytm ignoruje czas pomiędzy krokami w przypadku, gdy tlen wykroczy poza martwą strefę. Ustawienie na 0 wyłącza tą funkcję.",
        "arduinoname": "Bypass"
    },
    {
        "name": "calculatedSteep",
        "char": "CS",
        "payload": true,
        "fullname": "Włącznik liczonego progu szybkiego spadku",
        "description": "Ustawione na 1 powoduje, że algorytm używa obliczonego progu szybkiego spadku. Liczony próg pozwala uzyskać potencjalnie lepszy spadek. Ustawienie na 0 wyłącza tą funkcję.",
        "arduinoname": "Calculated Steep"
    },
    {
        "name": "steepExponent",
        "char": "SE",
        "payload": true,
        "fullname": "Eksponenta liczonego szybkiego spadku",
        "description": "Agresywność liczonego progu spadku. Wyższe wartości spowodują, że algorytm dopuszcza większy spadek. Wartości: 0.0 - 100.0",
        "arduinoname": "Calculated Steep"
    },
    {
        "name": "steepAddition",
        "char": "SA",
        "payload": true,
        "fullname": "Dodatek liczonego szybkiego spadku",
        "description": "Wartość dodawana do liczonego spadku, powoduje przesunięcie liczonego spadku o ustawioną wartość tzn. jeżeli policzony spadek to 0.5, to jeżeli ta wartość ustawiona jest na 0.25 spowoduje, że policzony spadek będzie wynosił ostatecznie 0.75. Wartości: 0.0 - 100.0.",
        "arduinoname": "Steep Addition"
    },
    {
        "name": "alphaAgressive",
        "char": "AE",
        "payload": true,
        "fullname": "Próg dodatkowej korekty",
        "description": "Jeżeli wartość alphy regresji liniowej (tzn. tempo zmiany tlenu w kierunku odstępwa od zadanej) osiągnie podaną wartość, spowoduje to dodatkowy krok serwa. Wartości: 0.0 - 100.0.",
        "arduinoname": "Alpha Agressive"
    },
    {
        "name": "alphaRest",
        "char": "AR",
        "payload": true,
        "fullname": "Próg alphy dla spoczynku",
        "description": "Jeżeli wartość alphy regresji liniowej będzie mniejsza od podanej wartości, spowoduje to przejście algorytmu w stan spoczynku. Wartości: 0.0 - 100.0.",
        "arduinoname": "Alpha Rest"
    },
    {
        "name": "stdDevRest",
        "char": "SR",
        "payload": true,
        "fullname": "Próg odchylenia standarowego dla spoczynku",
        "description": "Jeżeli wartość odchylenia standarowego będzie mniejsza od podanej wartości, spowoduje to przejście algorytmu w stan spoczynku. Wartości: 0.0 - 100.0.",
        "arduinoname": "StdDev Rest"
    },
    {
        "name": "fallBreak",
        "char": "FB",
        "payload": true,
        "fullname": "Wyłącznik hamulca",
        "description": "Jeżeli ustawione na 1, to funkcja hamulca jest włączone. Jeżeli na 0 to wyłączona.",
        "arduinoname": "Fall Break"
    },
    {
        "name": "breakStep",
        "char": "BS",
        "payload": true,
        "fullname": "Krok hamulca",
        "description": "O ile hamulec ma ruszyć serwo. Wartości: 0 - 1000.",
        "arduinoname": "Break Step"
    },
];

buttons.forEach((b) => {
    button(b.name, b.char, b.payload, b.fullname, b.description, b.arduinoname)
})
