import { createOrganization, loadOrganization, setSendingFailed } from "../../Actions/SampleAction";
import { Member, Organization, Participant, Result, Run, SkillLevel, Size, RunCategory, KombiResult, ExtendedResult, Tournament } from "../../types/ResponseTypes"
import { CommonReducerType } from '../../Reducer/CommonReducer';
import { faultFactor, maxTimeFactorA0A1A2, maxTimeFactorA3, minSpeedA3, minSpeedJ3, offsetFactor, refusalFactor } from "./AgilityPO";
import * as forge from 'node-forge';
import { Dispatch } from "react";

export const doRequest = async (method: string, path: string, data: any, dispatch: Dispatch<any>, ignoreFails?: boolean) => {
    try {
        const resp = await fetch(window.globalTS.DOMAIN + path,
            {
                credentials: 'include',
                method: method,
                headers: { "Content-type": "application/json", "Access-Control-Allow-Origin": "/*" },
                body: method.toUpperCase() !== "GET" ? JSON.stringify(data) : null
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
        console.log(error)
        if (ignoreFails === undefined || !ignoreFails) {
            dispatch(setSendingFailed(true))
        }
        return { code: 503 /*Service unavailable*/, content: {} }
    }
};

export const doGetRequest = async (path: string, dispatch: Dispatch<any>, ignoreFails?: boolean) => {
    return doRequest("GET", path, {}, dispatch, ignoreFails)
};
export const doPostRequest = async (path: string, data: any, dispatch: Dispatch<any>, ignoreFails?: boolean) => {
    return doRequest("POST", path, data, dispatch, ignoreFails)
};

export const doPostRequestRawBody = async (path: string, body: any) => {
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


export const safeMemberName = (member: Member) => {
    return member.alias === "" ? member.name : member.alias
}

export const stringToColor = (string: String) => {
    let hash = 0;
    let i;

    /* eslint-disable no-bitwise */
    for (i = 0; i < string.length; i += 1) {
        hash = string.charCodeAt(i) + ((hash << 5) - hash);
    }

    let color = '#';

    for (i = 0; i < 3; i += 1) {
        const value = (hash >> (i * 8)) & 0xff;
        color += `00${value.toString(16)}`.slice(-2);
    }
    /* eslint-enable no-bitwise */

    return color;
}

export const calculateAvatarText = (text: String) => {
    const emojiFilterd = text.match(/\p{Emoji}+/gu)
    const emoji = emojiFilterd ? emojiFilterd[0] : "?"
    const short = text.substring(0, 2)

    return emoji !== "?" ? emoji : short
}

export const sizeToString = (size: Size) => {
    if (size === Size.Small) {
        return "Small"
    } else if (size === Size.Medium) {
        return "Medium"
    } else if (size === Size.Intermediate) {
        return "Intermediate"
    }
    else if (size === Size.Large) {
        return "Large"
    }
}

export const classToString = (runClass: Run) => {
    if (runClass === Run.A0) {
        return "A0"
    } else if (runClass === Run.A1) {
        return "A1"
    } else if (runClass === Run.A2) {
        return "A2"
    }
    else if (runClass === Run.A3) {
        return "A3"
    }
    else if (runClass === Run.J0) {
        return "A0 2.Chance/Spiel 0"
    }
    else if (runClass === Run.J1) {
        return "A1 2.Chance/Spiel 1"
    }
    else if (runClass === Run.J2) {
        return "J2"
    }
    else if (runClass === Run.J3) {
        return "J3"
    }
}

export const runClassToString = (runClass: SkillLevel) => {
    if (runClass === SkillLevel.A0) {
        return "A0"
    } else if (runClass === SkillLevel.A1) {
        return "A1"
    } else if (runClass === SkillLevel.A2) {
        return "A2"
    }
    else if (runClass === SkillLevel.A3) {
        return "A3"
    }
}

export const isYouthParticipant = (birthDate: string, tournamentDate: Date) => {
    // A participant is considered youth if they are younger than 18 years or turned 18 in the current year
    const birthYear = new Date(birthDate).getFullYear()
    const currentYear = tournamentDate.getFullYear()
    return currentYear - birthYear <= 18
}

export const storePermanent = (organization: string, value: Organization) => {
    if (organization === "" || value.name === "") { return }
    if (value.name === "") {
        value.name = organization
    }
    window.localStorage.setItem(organization, JSON.stringify(value))
}

export const loadPermanent = (t_organization: string, dispatch: any, common: CommonReducerType, forceLoad?: boolean) => {
    const item = window.localStorage.getItem(t_organization);
    if (item === null) {

        const organization = {
            name: t_organization,
            turnaments: []
        }
        storePermanent(t_organization, organization)
        dispatch(createOrganization(organization))
        return organization
    } else {
        if (common.organization.name !== t_organization || forceLoad) {
            const org: Organization = JSON.parse(item)
            dispatch(loadOrganization(org))
            return org
        }
    }
}

export const getResultFromParticipant = (runClass: Run, participant: Participant) => {
    const isA = runClass % 2 === 0
    const result = isA ? participant.resultA : participant.resultJ
    return result
}

export const runTimeToString = (time: number) => {
    if (time === -2) {
        return "-"
    } else if (time === -1) {
        return "Dis"
    }
    return `${time.toFixed(2)}s`
}

export const runTimeToStringClock = (time: number) => {
    if (time === -2) {
        return "Warte auf Start"
    } else if (time === -1) {
        return "Disqualifiziert"
    }
    return `${time.toFixed(2)}s`
}

export const getParticipantsForRun = (participants: Participant[], run: SkillLevel, size: Size, youth?: boolean) => {
    if (youth === undefined) {
        return participants?.filter(p => p.skillLevel === run && p.size === size && p.registered)
    } else {
        return participants?.filter(p => p.skillLevel === run && p.size === size && p.registered && p.isYouth === youth)
    }
}

export const getNumberOfParticipantsForRun = (participants: Participant[], run: SkillLevel, size: Size, youth?: boolean) => {
    return getParticipantsForRun(participants, run, size, youth).length
}

export const getParticipantFromStartNumber = (participants: Participant[], startNumber: number) => {
    return participants.find(p => p.startNumber === startNumber)
}

export const getNumberOfParticipantsForRunWithResult = (participants: Participant[], run: Run, size: Size) => {
    const filteredParticipants = getParticipantsForRun(participants, Math.floor(run / 2), size)
    return filteredParticipants.filter(p => getResultFromParticipant(run, p).time > -2).length
}

export const compareParticipants = (oldParticipant: Participant, newParticipant: Participant) => {
    let oldName = String(oldParticipant.name).trim()
    let newName = String(newParticipant.name).trim()
    let oldDog = String(oldParticipant.dog).trim()
    let newDog = String(newParticipant.dog).trim()

    oldName = oldName.replace("undefined", "")
    newName = newName.replace("undefined", "")
    oldDog = oldDog.replace("undefined", "")
    newDog = newDog.replace("undefined", "")

    return oldName === newName && oldDog === newDog
}

export const runToRunClass = (run: Run) => {
    return Math.floor(run / 2)
}

export const getParcoursFaults = (result: Result) => {
    return result.faults * faultFactor + result.refusals * refusalFactor
}

export const getTimeFaults = (result: Result, standardTime: number) => {
    const timeFaults = result.time > standardTime ? result.time - standardTime : 0

    // only two decimal places
    return Math.round(timeFaults * 100) / 100
}

export const getTotalFaults = (result: Result, standardTime: number) => {
    return getParcoursFaults(result) + getTimeFaults(result, standardTime)
}

export const getRunCategory = (run: Run) => {
    return run % 2 === 0 ? RunCategory.A : RunCategory.J
}

type SandardTimeFunction = (run: Run, size: Size, participants: Participant[], parcoursLenght: number, parcoursSpeed: number) => number

export const standardTime: SandardTimeFunction = (run: Run, size: Size, participants: Participant[], parcoursLenght: number, parcoursSpeed: number) => {
    if (parcoursSpeed > 0) {
        if (run / 2 < SkillLevel.A3) {
            //Regeln für A0, A1, A2
            return parcoursLenght / parcoursSpeed
        }
        else {

            //Regeln für A3
            const runParticipants = getParticipantsForRun(participants, runToRunClass(run), size)

            const results = runParticipants.map(p => getResultFromParticipant(run, p)).filter(r => r.time > 0) //Has time and no Dis

            if (results.length === 0) {
                return minSpeedA3
            }

            //Get result with lowest parcours faults
            const resultWithLowestFaults = results.reduce((prev, current) => {
                return getParcoursFaults(prev) < getParcoursFaults(current) ? prev : current
            })

            //All results with lowest parcours faults
            const resultsWithLowestFaults = results.filter(r => getParcoursFaults(r) === getParcoursFaults(resultWithLowestFaults))

            //Get result with lowest time
            const resultWithLowestTime = resultsWithLowestFaults.reduce((prev, current) => {
                return prev.time < current.time ? prev : current
            })

            const calculatedStandardTime = Math.ceil(resultWithLowestTime.time * offsetFactor)

            /* The standard time needs to have a minimum run speed -> https://www.vdh.de/fileadmin/media/hundesport/agility/2020/Standardzeit_A3_JP3_Faktor_2020_22-12-2019_V1.pdf */

            const standardTimeRunSpeed = parcoursLenght / calculatedStandardTime

            if ((run === Run.A3 && standardTimeRunSpeed >= minSpeedA3) || (run === Run.J3 && standardTimeRunSpeed >= minSpeedJ3)) {
                /* Case A from PDF */
                return calculatedStandardTime
            } else {
                /* Case B from PDF */
                if (run === Run.A3) {
                    return parcoursLenght / minSpeedA3
                } else if (run === Run.J3) {
                    return parcoursLenght / minSpeedJ3
                }
            }
        }
    }

    return minSpeedA3
}

export const maximumTime: (run: Run, standardTime: number) => number = (run: Run, standardTime: number) => {
    if (run / 2 < SkillLevel.A3) {
        //Regeln für A0, A1, A2
        return Math.ceil(standardTime * maxTimeFactorA0A1A2)
    }
    else {
        //Regeln für A3
        return Math.ceil(standardTime * maxTimeFactorA3)
    }
}

export const getRanking: (participants: Participant[] | undefined, run: Run, calculatedStandardTime: number, size?: Size) => ExtendedResult[] =
    (participants: Participant[] | undefined, run: Run, calculatedStandardTime: number, size?: Size) => {
        if (!participants) { return [] }
        //Filter out participants without result
        const filteredParticipants = participants.filter(p => {
            const result = getResultFromParticipant(run, p);
            return result.time !== -2 && p.skillLevel === runToRunClass(run) && (p.size === size || size === undefined);
        });

        const sorted = filteredParticipants?.sort((a, b) => {
            const resultA = getResultFromParticipant(run, a)
            const resultB = getResultFromParticipant(run, b)
            const totalFaultsA = getTotalFaults(resultA, calculatedStandardTime)
            const totalFaultsB = getTotalFaults(resultB, calculatedStandardTime)
            if (resultA.time === -1) {
                return 1
            } if (resultB.time === -1) {
                return -1
            }
            if (totalFaultsA === totalFaultsB) {
                return resultA.time - resultB.time
            }
            return totalFaultsA - totalFaultsB
        })

        if (filteredParticipants.length === 0) {
            return []
        }

        //Participants with same time and total faults get the same rank
        let currentrank = 1
        let lastResult = getResultFromParticipant(run, sorted[0])
        let lastTotalFaults = getTotalFaults(lastResult, calculatedStandardTime)
        let lastTime = lastResult.time
        let lastRank = 1
        let sameCounter = -1
        const results = sorted.map((p, index) => {
            const result = getResultFromParticipant(run, p)
            const totalFaults = getTotalFaults(result, calculatedStandardTime)
            const time = result.time
            if (time === -1) {
                return { result: result, participant: p, rank: -1, timefaults: -1 }
            }
            if (totalFaults === lastTotalFaults && time === lastTime) {
                sameCounter++
                return { result: result, participant: p, rank: lastRank, timefaults: getTimeFaults(result, calculatedStandardTime) }
            }
            currentrank++
            currentrank += sameCounter
            sameCounter = 0
            lastResult = result
            lastTotalFaults = totalFaults
            lastTime = time
            lastRank = currentrank

            return { result: result, participant: p, rank: currentrank, timefaults: getTimeFaults(result, calculatedStandardTime) }
        })
        return results

    }

export const getKombiRanking: (participants: Participant[], skill: SkillLevel, size: Size, standardTimeA: number, standardTimeJ: number) => KombiResult[] =
    (participants: Participant[], skill: SkillLevel, size: Size, standardTimeA: number, standardTimeJ: number) => {



        //Filter out participants who have results in both A and J
        const filteredParticipants = participants.filter(p => {
            const resultA = getResultFromParticipant(skill * 2, p)
            const resultJ = getResultFromParticipant(skill * 2 + 1, p)
            return resultA.time > 0 && resultJ.time > 0 && p.size === size && p.skillLevel === skill
        });

        /* Initialize array of participants with their combi results, initially all -1 */
        const combiResults = filteredParticipants.map(p => {
            return {
                participant: p,
                totalFaults: 0,
                totalTime: 0,
                kombi: -1
            }
        })



        /* Calculate total faults and total time for each participant */
        filteredParticipants.forEach(p => {
            const resultA = getResultFromParticipant(skill * 2, p)
            const resultJ = getResultFromParticipant(skill * 2 + 1, p)

            combiResults.find(c => c.participant === p)!.totalFaults = getTotalFaults(resultA, standardTimeA) + getTotalFaults(resultJ, standardTimeJ)
            combiResults.find(c => c.participant === p)!.totalTime = resultA.time + resultJ.time
        })

        /* Sort participants by total faults and then total time */
        combiResults.sort((a, b) => {
            if (a.totalFaults === b.totalFaults) {
                return a.totalTime - b.totalTime
            }
            return a.totalFaults - b.totalFaults
        })

        /* Assign ranks to participants */
        combiResults.forEach((c, index) => {
            /* If participant is not in filteredParticipants, set rank to -1 */
            if (!filteredParticipants.includes(c.participant)) {
                c.kombi = -1
                return
            }
            c.kombi = index + 1
        })

        return combiResults
    }

export const getRating = (time: number, totalFaults: number) => {
    /* V SG G OB DIS */
    // https://www.vdh.de/fileadmin/media/hundesport/agility/2018/Ordnung/VDH_TEil_FCI-PO_Agility_2018_2018-05-17_V-6_HP.pdf
    // Seite 50 -> Suche nach 'OHNE BEWERTUNG' im PDF
    if (time === -2) {
        return "-"
    }

    if (time === -1) {
        return "DIS"
    }
    if (totalFaults < 1) {
        return "V0"
    } else if (totalFaults < 6) {
        return "V"
    } else if (totalFaults < 16) {
        return "SG"
    }
    else if (totalFaults < 26) {
        return "G"
    } else {
        return "OB"
    }
}

export const getRatingIndex = (rating: string) => {
    if (rating === "V" || rating === "V0") {
        return 0
    } else if (rating === "SG") {
        return 1
    } else if (rating === "G") {
        return 2
    } else if (rating === "OB") {
        return 3
    }
    return 4
}

export const openSerial = async (setSerial: (port: any) => void) => {
    let port = await (navigator as any).serial.requestPort();

    await port.open({ baudRate: 115200, bufferSize: 1000000 });

    setSerial(port)
}

export const startSerial = async (onmessage: (message: string) => void, onconnected: () => void, ondisconnect: () => void) => {
    let port
    let message = ""
    try {
        try {
            port = await (navigator as any).serial.requestPort();

            await port.open({ baudRate: 115200, bufferSize: 1000000 });
            // Listen to data coming from the serial device.

            const textDecoderStream = new TextDecoderStream('utf-8');
            const decodedStream = port.readable.pipeThrough(textDecoderStream);


            const reader = decodedStream.getReader();

            onconnected()
            let { value, done } = await reader.read();
            let activeMessage = false;
            message = ""
            while (!done) {
                if (value.includes("$") && value.includes("#")) {
                    onmessage(value.substring(value.indexOf("$") + 1, value.indexOf("#")))
                } else if (value.includes("$")) {
                    activeMessage = true;
                    message += value.substring(value.indexOf("$") + 1);
                } else if (value.includes("#")) {
                    activeMessage = false;

                    message += value.substring(0, value.indexOf("#"));
                    onmessage(message)
                    message = ""
                }
                if (activeMessage) {
                    message += value
                }
                ({ value, done } = await reader.read());
            }

        } catch (e) {
            console.log(e)
            ondisconnect()
            //dispatch(openToast({ message: "Die Verbindung zur Zeitmessanlage wurde unterbrochen, klicke auf verbinden Knopf", type: "error", headline: "Fehler mit Zeitmessanlage", duration: 15000 }))
        }
        //port.close()
    } catch (e) {

    }
}

export const setMaxTime = (currentRun: Run,
    calculatedStandardTime: number,
    participantsForRun: Participant[],
    allParticipants: Participant[]
) => {
    const maxTime = maximumTime(currentRun, calculatedStandardTime)


    let tempParticipants = allParticipants


    participantsForRun.forEach(toCheck => {

        /* Get all participants of this run*/
        tempParticipants = tempParticipants.map(p => {
            if (p.startNumber === toCheck.startNumber) {
                //  check if a or j
                if (getRunCategory(currentRun) === RunCategory.A) {
                    return { ...p, resultA: { ...p.resultA, time: (p.resultA.time > maxTime) ? -1 : p.resultA.time } }
                } else {
                    return { ...p, resultJ: { ...p.resultJ, time: (p.resultJ.time > maxTime) ? -1 : p.resultJ.time } }
                }
            }
            return p
        })
    })

    return tempParticipants
}

export const fixDis = (currentRun: Run,
    participantsForRun: Participant[],
    allParticipants: Participant[],
) => {

    let tempParticipants = allParticipants

    participantsForRun.forEach(toCheck => {

        /* Get all participants of this run*/
        tempParticipants = tempParticipants.map(p => {
            if (p.startNumber === toCheck.startNumber) {
                //  check if a or j
                if (getRunCategory(currentRun) === RunCategory.A) {
                    return { ...p, resultA: { ...p.resultA, time: (p.resultA.refusals >= 3) ? -1 : p.resultA.time } }
                } else {
                    return { ...p, resultJ: { ...p.resultJ, time: (p.resultJ.refusals >= 3) ? -1 : p.resultJ.time } }
                }
            }
            return p
        })

    })

    return tempParticipants
}

export const updateDatabase = async (turnament: Tournament | undefined, memberName: string, dispatch: Dispatch<any>) => {
    if (turnament) {
        const year = new Date(turnament?.date).getFullYear()
        const month = new Date(turnament?.date).getMonth() + 1
        const day = new Date(turnament?.date).getDate()
        const date = `${year}-${month < 10 ? "0" + month : month}-${day < 10 ? "0" + day : day}`
        const resp = await doPostRequest(`${memberName}/tournament/${date}`, turnament ? turnament : null, dispatch)//Updates the database
        if (resp.code === 503) {
            return false
        }
    }
    return true
}

export const favoriteIdenfitier = (participant: Participant) => {
    return `${participant.name}-${participant.dog}`
}

export const checkIfFavorite = (participant: Participant, favorites: string | undefined | null) => {

    if (favorites === undefined || favorites === null) { return false }

    let isFavorite = false

    favorites.split(";").forEach(favorite => {

        if (favorite === favoriteIdenfitier(participant)) {
            isFavorite = true
            return
        }
    })
    return isFavorite
}

export const wait = async (ms: number) => {
    return new Promise(r => setTimeout(r, ms));
}


const publicKey = forge.pki.publicKeyFromPem(`
    -----BEGIN RSA PUBLIC KEY-----
    MIIBCgKCAQEAsbc4TN4l3E8VJAC/U/WMUHEWomIbz32+EyDv7BzqtkWUlBtljjtk
    Rz/g313i4XLj3rLjVJ4RL4zs3Qw/NDzv6fwh895o3mL5/b/arb3TDzjTxthe7qsf
    e51+GlmgYh/hJEqORmokSxMo50n2BJVKbnGGRg/2T2viWsVIZGPFMUBuk/h5lcKd
    yXsts7Wf0MVC7z+zbpcsiMKENcelZEqPmhIlEdOIgdqaDNgDF4yLtNlbb6allwwr
    h9HLrZ4W7KBf2tn5q2dXubiPyVbdUmdQaEj2fN+RaZrxjQn8vTVqyL8x99UYGnc4
    5t0+9iGnyfxpwqthzgQ+j/dBNsfMZ+4rAQIDAQAB
    -----END RSA PUBLIC KEY-----
                `.trim())

export const verifySignature = (message: string, signature: string): boolean => {
    const md = forge.md.sha256.create();
    md.update(message, 'utf8');
    try {
        return publicKey.verify(md.digest().bytes(), forge.util.decode64(signature));
    } catch (e) {
        return false;
    }
}

export const moveParticipantInStartList = (direction: "up" | "down", run: Run, size: Size, allParticipants: Participant[], startNumber: number) => {
    const relevantParticipantsSorted = allParticipants.filter(p => p.skillLevel === runToRunClass(run) && p.size === size).sort((a, b) => a.sorting - b.sorting)

    //Get startnumber and sorting of the participant that should be moved and the one that should be swapped with
    const participantToMove = relevantParticipantsSorted.find(p => p.startNumber === startNumber)
    const participantToSwapWith = direction === "up" ? relevantParticipantsSorted.find(p => p.sorting === participantToMove!.sorting - 1) : relevantParticipantsSorted.find(p => p.sorting === participantToMove!.sorting + 1)

    //If there is no participant to swap with, return the original list
    if (!participantToSwapWith) {
        return allParticipants
    }

    //Swap the sorting of the two participants while keeping references
    const tempSorting = participantToMove!.sorting
    participantToMove!.sorting = participantToSwapWith.sorting
    participantToSwapWith.sorting = tempSorting

    return allParticipants.map(p => {
        if (p.startNumber === participantToMove!.startNumber) {
            return participantToMove!
        } else if (p.startNumber === participantToSwapWith.startNumber) {
            return participantToSwapWith
        }
        return p
    })
}

export const stringToSkillLevel = (skillLevel: string) => {
    if (skillLevel === "A0") {
        return SkillLevel.A0
    } else if (skillLevel === "A1") {
        return SkillLevel.A1
    } else if (skillLevel === "A2") {
        return SkillLevel.A2
    } else if (skillLevel === "A3") {
        return SkillLevel.A3
    }
    return SkillLevel.A0
}

export const stringToSize = (size: string) => {
    if (size === "Small") {
        return Size.Small
    } else if (size === "Medium") {
        return Size.Medium
    } else if (size === "Intermediate") {
        return Size.Intermediate
    } else if (size === "Large") {
        return Size.Large
    }
    return Size.Small
}