import Cookies from "js-cookie";

export const doPostRequest = async (path, data) => {
    try {
        const resp = await fetch(window.globalTS.DOMAIN + path,
            {
                credentials: 'include',
                method: "POST",
                headers: { "Content-type": "application/json", "Access-Control-Allow-Origin": "/*" },
                body: JSON.stringify(data)
            });

        const status_code = resp.status

        const userJson = await resp.json();

        return { code: status_code, content: userJson }
    } catch (error) {
        return { code: 503 /*Service unavailable*/, content: {} }
    }

};


export const doRequest = async (method, path, data) => {
    try {
        const resp = await fetch(window.globalTS.DOMAIN + path,
            {
                credentials: 'include',
                method: method,
                headers: { "Content-type": "application/json", "Access-Control-Allow-Origin": "/*" },
                body: JSON.stringify(data)
            });
        const status_code = resp.status
        if (status_code === 200) {
            const userJson = await resp.json();

            return { code: status_code, content: userJson }
        } else if (status_code === 403) {
            return { code: status_code }
        } else {
            return { code: status_code }
        }
    } catch (error) {
        return { code: 503 /*Service unavailable*/, content: {} }
    }
};

export const doPostRequestRawBody = async (path, body) => {
    try {
        const resp = await fetch(window.globalTS.DOMAIN + path,
            {
                credentials: 'include',
                method: "POST",

                body: body
            });
        const status_code = resp.status
        if (status_code === 200) {
            const userJson = await resp.json();

            return { code: status_code, content: userJson }
        } else if (status_code === 403) {
            return { code: status_code }
        } else {
            return { code: status_code }
        }
    } catch (error) {
        return { code: 503 /*Service unavailable*/, content: {} }
    }
};

export const doGetRequest = async (path) => {
    try {
        const userInput = await fetch(window.globalTS.DOMAIN + path,
            {
                credentials: 'include',
                method: "GET",
                headers: { "Content-type": "application/json", "Access-Control-Allow-Origin": "*" },
            });

        const status_code = userInput.status

        if (status_code === 200) {
            const userJson = await userInput.json();
            return { code: status_code, content: userJson }
        } else if (status_code === 403) {
            return { code: status_code }
        } else {
            return { code: status_code }
        }
    } catch (error) {
        return { code: 503 /*Service unavailable*/, content: {} }
    }
};

export const downloadJSON = async (path, filename) => {
    return fetch(window.globalTS.DOMAIN + path, {
        method: 'GET',
        credentials: 'include',
        headers: {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*'
        },
    })
        .then((response) => {
            if (response.status === 200) {
                return response.blob()
            } else {
                return 400
            }
        })
        .then((blob) => {
            if (blob === 400) {
                return 400
            }
            // Create blob link to download
            const url = window.URL.createObjectURL(
                new Blob([blob]),
            );
            const link = document.createElement('a');
            link.href = url;
            link.setAttribute(
                'download',
                filename,
            );

            // Append to html link element page
            document.body.appendChild(link);

            // Start download
            link.click();

            // Clean up and remove the link
            link.parentNode.removeChild(link);
        });
};

export const getAndStore = (path, stateFunction) => {
    const getInfos = async () => {
        const req = await doGetRequest(path)

        if (req.code === 200) {
            stateFunction(req.content)
            return req.content
        }
    }
    getInfos()
};

export const getHoursFromMember = (members) => {
    var currentWork = 0
    members.currentWork.forEach(element => {
        currentWork += element.hours
    });

    var maxWork = 0
    members.maxWork.forEach(element => {
        maxWork += element.hours
    });

    return [currentWork, maxWork]
}

export const secureRandomNumber = () => {
    var array = new Uint32Array(20);
    crypto.getRandomValues(array);
    var sum = 0
    for (var i = 0; i < array.length; i++) {
        sum += array[i];
    }
    return sum
}

export const dateToString = (date) => {
    return date.getDate().toString().padStart(2, '0') + "." + (date.getMonth() + 1).toString().padStart(2, '0') + "." + date.getFullYear().toString()
}

export const dateToURLString = (date) => {
    return (date.getFullYear().toString() + "-" + (date.getMonth() + 1).toString().padStart(2, '0') + "-" + date.getDate().toString().padStart(2, '0'))
}

export const timeToString = (date) => {
    const startMinutes = date.getMinutes().toString().padStart(2, '0');
    const startHours = date.getHours().toString().padStart(2, '0');
    return startHours + ":" + startMinutes + " Uhr"
}

export const datetimeToString = (date) => {
    return dateToString(new Date(date)) + " - " + timeToString(new Date(date))
}

export const timeTupleToString = (times) => {
    const startMinutes = times[0].getMinutes().toString().padStart(2, '0');
    const startHours = times[0].getHours().toString().padStart(2, '0');
    const endMinutes = times[1].getMinutes().toString().padStart(2, '0');
    const endHours = times[1].getHours().toString().padStart(2, '0');

    return startHours + ":" + startMinutes + " - " + endHours + ":" + endMinutes + " Uhr"
}

export const getmemberIDCookie = () => {
    const rawMemberID = Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID")

    return rawMemberID ? parseInt(rawMemberID) : null
}


