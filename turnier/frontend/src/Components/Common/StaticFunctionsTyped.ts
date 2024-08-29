import { createOrganization, loadOrganization } from "../../Actions/SampleAction";
import { Member, Organization, Participant, Result, Run, SkillLevel, Size, RunCategory } from "../../types/ResponseTypes"
import { CommonReducerType } from '../../Reducer/CommonReducer';
import { faultFactor, maxTimeFactorA0A1A2, maxTimeFactorA3, minSpeedA3, minSpeedJ3, offsetFactor, refusalFactor } from "./AgilityPO";

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
        window.localStorage.setItem(t_organization, JSON.stringify({ organization }))
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
    return participants?.filter(p => p.class === run && p.size === size)
}

export const getNumberOfParticipantsForRun = (participants: Participant[], run: SkillLevel, size: Size) => {
    return getParticipantsForRun(participants, run, size).length
}

export const getNumberOfParticipantsForRunWithResult = (participants: Participant[], run: Run, size: Size) => {
    const filteredParticipants = getParticipantsForRun(participants, run / 2, size)
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

export const getRanking = (participants: Participant[] | undefined, run: Run, calculatedStandardTime: number) => {
    if (!participants) { return [] }
    return participants?.sort((a, b) => {
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
        return { result: result, participant: p, rank: index + 1 }
    })
}