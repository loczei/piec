const oxygen_ctx = document.getElementById('tlen');

let oxygen = new Chart(oxygen_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Poziom tlenu',
            data: [],
            fill: false,
        },
        {
            label: 'Zadany tlen',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
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
        datasets: [{
            label: 'Punkt równowagi',
            data: [],
            fill: false,
        },
        {
            label: 'Wychylenie serwa',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
    }
});

const topServo_ctx = document.getElementById('serwo-gorne');

let topServo = new Chart(topServo_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Wychylenie serwa',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
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

function deleteHistory(target) {
    if (target.data.datasets[0].data.length > history) {
        target.data.datasets[0].data = target.data.datasets[0].data.slice(target.data.datasets[0].data.length - history);
        target.data.datasets[1].data = target.data.datasets[1].data.slice(target.data.datasets[1].data.length - history);
        target.data.labels = target.data.labels.slice(target.data.labels.length - history);
    }
}

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

        deleteHistory(oxygen);

        oxygen.update();

        servo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        servo.data.datasets[0].data.push(msg.balance + msg.balanceCenter);
        servo.data.datasets[1].data.push(msg.servo);

        deleteHistory(servo);

        servo.update();

        topServo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        topServo.data.datasets[0].data.push(msg.topServo);

        if (topServo.data.datasets[0].data.length > history) {
            topServo.data.datasets[0].data = topServo.data.datasets[0].data.slice(topServo.data.datasets[0].data.length - history);
            topServo.data.labels = servo.data.labels.slice(topServo.data.labels.length - history);
        }

        topServo.update();

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

button("zadana", "T", true);
button("odciecie", "C", true);
button("odciecie-servo", "O", true);
button("pompy", "R", false);
button("serwo", "S", false);
button("min-angle", "I", true, "Minimalne wychylenie serwa", "Kontroluje minimalne wychelenie serwa głównego (tego które działa płynnie). <br> Wartości: 1-4096", "minServo");
button("max-angle", "A", true, "Maksymalne wychylenie serwa", "Kontroluje maksylmalne wychelenie serwa głównego (tego które działa płynnie). <br> Wartości: 1-4096", "maxServo");
button("multi-real", "U", true, "Mnożnik w czasie rzeczywistym", "Kontroluje jak szybko porusza się główne serwo. <br> Wartości (liczba rzeczywista tzn. z kropką np. 7.65): sens mają liczby od 1.0 do około 10.0", "multiReal");
button("boost-real", "E", true, '"Dodatek" w czasie rzeczywstym', '"Dodatek", który jest dodawany do wychylenie serwa kiedy różnica tlenu będzie wynosiła więcej niż 3% tlenu.  <br> Wartości (liczba rzeczywista tzn. z kropką np. 7.65): sens mają liczby wzwyż od 1.0, ale wartość powinna być jak najmiejsza. Można ustawić na 0.0, aby wyłączyć ten "dodatek"', "boostReal");
button("multi-max", "M", true, "Maksymalne wychylenie serwa w czasie rzeczywistym: ", "Kontroluje o ile może poruszyć się serwo. <br> Wartości (liczba rzeczywista tzn. z kropką np. 7.65): sens mają małe krotności mnożnika np. kiedy mnożnik jest równy 5.0, to tą wartość można ustawić na 20.0. Można też ustawić tę liczbę na bardzo dużo (1000.0 powinno wystarczyć), aby wyłączyć ten mechanizm", "multiMax");
button("servo-balance-cooldown", "V", true, "Okres punktu równowagi (w milisekundach)", "Kontroluja jak często zmienia się punkt równowagi. <br> Wartości: dowolne dodatnie, sens mają wartości do 10000, bo większe wartości zabiją przeznaczenie tego mechanizmu. Najlepiej trzymać tą wartośc między 1 a 2000", "servoBalanceCooldown");
button("balance-multi", "L", true, "Wartość zmiany punktu równowagi", "Kontroluje o ile zmienia się punkt równowagi. <br> Wartości: najlepiej zostawić na 1 i zmieniać okres.", "balanceMulti");
button("max-balance", "X", true, "Maksymalne odychlenie od punktu równowagi", "Kontroluje jak bardzo serwo może się wychylić od punktu równowagi. <br> Wartości: od około 30 do 200 mają sens. Można też ustawić tę liczbę na bardzo dużo (1000 powinno wystarczyć), aby wyłączyć ten mechanizm", "maxBalance");
button("top-open", "P", true, "Prędkość otwierania drugiego serwa", "Jeżeli serwo ma się otwierać natychmiastowo, ustawić na 1000. Jeżeli nie to ustawić pomiędzy 1 a 200.", "topOpenSpeed");
button("top-close", "Z", true, "Prędkość zamykania drugiego serwa", "Jeżeli serwo ma się otwierać natychmiastowo, ustawić na 1000. Jeżeli nie to ustawić pomiędzy 1 a 200.", "topCloseSpeed");
button("top-min", "J", true, "Minimalne wychylenie drugiego serwa", "Kontroluje minimalne wychelenie serwa drugiego (tego które skacze). <br> Wartości: 1-4096", "topMinServo");
button("top-max", "G", true, "Maksymalne wychylenie drugiego serwa", "Kontroluje maksymalne wychelenie serwa drugiego (tego które skacze). <br> Wartości: 1-4096", "topMaxServo");