import { createOrganization, loadOrganization } from "../../Actions/SampleAction";
import { Member, Organization, Participant, Result, Run, SkillLevel, Size, RunCategory, KombiResult, ExtendedResult, Tournament } from "../../types/ResponseTypes"
import { CommonReducerType } from '../../Reducer/CommonReducer';
import { faultFactor, maxTimeFactorA0A1A2, maxTimeFactorA3, minSpeedA3, minSpeedJ3, offsetFactor, refusalFactor } from "./AgilityPO";
import { doPostRequest } from "./StaticFunctions";
import * as forge from 'node-forge';

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

export const storePermanent = (organization: string, value: Organization) => {
    if (organization === "" || value.name === "") { return }
    if (value.name === "") {
        value.name = organization
    }
    console.log(value)
    window.localStorage.setItem(organization, JSON.stringify(value))
}

export const loadPermanent = (params: any, dispatch: any, common: CommonReducerType) => {
    const t_organization = params.organization ? params.organization : ""
    const item = window.localStorage.getItem(t_organization);

    if (item === null) {

        const organization = {
            name: t_organization,
            turnaments: []
        }
        storePermanent(t_organization, organization)
        dispatch(createOrganization(organization))
    } else {
        if (common.organization.name !== t_organization) {
            const org: Organization = JSON.parse(item)
            dispatch(loadOrganization(org))
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

export const getParticipantsForRun = (participants: Participant[], run: SkillLevel, size: Size) => {
    return participants?.filter(p => p.skillLevel === run && p.size === size)
}

export const getNumberOfParticipantsForRun = (participants: Participant[], run: SkillLevel, size: Size) => {
    return getParticipantsForRun(participants, run, size).length
}

export const getParticipantFromStartNumber = (participants: Participant[], startNumber: number) => {
    return participants.find(p => p.startNumber === startNumber)
}

export const getNumberOfParticipantsForRunWithResult = (participants: Participant[], run: Run, size: Size) => {
    const filteredParticipants = getParticipantsForRun(participants, Math.floor(run / 2), size)
    return filteredParticipants.filter(p => getResultFromParticipant(run, p).time > -2).length
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

        return filteredParticipants?.sort((a, b) => {
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
        }).map((p, index) => {
            const result = getResultFromParticipant(run, p)

            return { result: result, participant: p, rank: result.time !== -1 ? index + 1 : -1, timefaults: getTimeFaults(result, calculatedStandardTime) }
        })
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
    //https://www.vdh.de/fileadmin/media/hundesport/agility/2018/Ordnung/VDH_TEil_FCI-PO_Agility_2018_2018-05-17_V-6_HP.pdf
    if (time === -2) {
        return "-"
    }

    if (time === -1) {
        return "DIS"
    }
    if (totalFaults === 0) {
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

    /*Print max time with what run */
    if (participantsForRun.length > 0)
        console.log(`Max time for ${classToString(currentRun)} ${sizeToString(participantsForRun[0].size)} is ${maxTime}`)

    let tempParticipants = allParticipants


    participantsForRun.forEach(toCheck => {

        /* Get all participants of this run*/
        tempParticipants = tempParticipants.map(p => {
            if (p.startNumber === toCheck.startNumber) {
                //  check if a or j
                if (getRunCategory(currentRun) === RunCategory.A) {
                    return { ...p, resultA: { ...p.resultA, time: (p.resultA.time > maxTime) ? -1 : p.resultA.time } }
                } else {
                    console.log(`Max time for ${classToString(currentRun)} ${sizeToString(participantsForRun[0].size)} is ${maxTime}`)
                    console.log({ ...p, resultJ: { ...p.resultJ, time: (p.resultJ.time > maxTime) ? -1 : p.resultJ.time } })
                    return { ...p, resultJ: { ...p.resultJ, time: (p.resultJ.time > maxTime) ? -1 : p.resultJ.time } }
                }
            }
            return p
        })


        /*print new participants*/
        console.log(tempParticipants)




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

export const updateDatabase = (turnament: Tournament | undefined) => {
    if (turnament) {
        const year = new Date(turnament?.date).getFullYear()
        const month = new Date(turnament?.date).getMonth() + 1
        const day = new Date(turnament?.date).getDate()
        const date = `${year}-${month < 10 ? "0" + month : month}-${day < 10 ? "0" + day : day}`
        console.log(date)
        doPostRequest(`0/${date}`, turnament ? turnament : null)//Updates the database
    }
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