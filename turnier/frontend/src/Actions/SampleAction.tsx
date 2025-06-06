import { ParticipantToPrint, ResultToPrint } from "../Reducer/CommonReducer"
import { Organization, Participant, Run, StickerInfo, Tournament } from "../types/ResponseTypes"

export const createOrganization = (org: Organization) => {
    return {
        type: "CREATE_ORGANIZATION",
        payload: org
    }
}

export const addTurnament = (turnament: Tournament) => {
    return {
        type: "ADD_TURNAMENT",
        payload: turnament
    }
}

export const removeTurnament = (turnament: Tournament) => {
    return {
        type: "REMOVE_TURNAMENT",
        payload: turnament
    }
}

export const loadOrganization = (org: Organization) => {
    return {
        type: "LOAD_ORGANIZATION",
        payload: org
    }
}

export const changeLength = (date: Date, run: Run, length: Number) => {
    return {
        type: "CHANGE_LENGTH",
        payload: { date: date, run: run, length: length }
    }
}

export const changeGame = (date: Date, run: Run, isGame: boolean) => {
    return {
        type: "CHANGE_GAME",
        payload: { date: date, run: run, isGame: isGame }
    }
}

export const changeSpeed = (date: Date, run: Run, speed: Number) => {
    return {
        type: "CHANGE_SPEED",
        payload: { date: date, run: run, speed: speed }
    }
}

export const changeDate = (oldDate: Date, newDate: Date) => {
    return {
        type: "CHANGE_DATE",
        payload: { oldDate: oldDate, newDate: newDate }
    }
}

export const changeJudge = (date: Date, judge: string) => {
    return {
        type: "CHANGE_JUDGE",
        payload: { date: date, judge: judge }
    }
}

export const changeTurnamentName = (date: Date, name: string) => {
    return {
        type: "CHANGE_TURNAMENT_NAME",
        payload: { date: date, name: name }
    }
}

export const addParticipant = (date: Date, participant: Participant) => {
    return {
        type: "ADD_PARTICIPANT",
        payload: { date: date, participant: participant }
    }
}

export const updateParticipant = (date: Date, participant: Participant) => {
    return {
        type: "UPDATE_PARTICIPANT",
        payload: { date: date, participant: participant }
    }
}

export const removeParticipant = (date: Date, participant: Participant) => {
    return {
        type: "REMOVE_PARTICIPANT",
        payload: { date: date, participant: participant }
    }
}

export const changeParticipants = (date: Date, participants: Participant[]) => {
    return {
        type: "CHANGE_PARTICIPANTS",
        payload: { date: date, participants: participants }
    }
}

export const addPrintResult = (results: ResultToPrint) => {
    return {
        type: "ADD_PRINT_RESULT",
        payload: results
    }
}

export const addPrintParticipant = (participants: ParticipantToPrint) => {
    return {
        type: "ADD_PRINT_PARTICIPANT",
        payload: participants
    }
}

export const addPrintSticker = (stickers: StickerInfo[]) => {
    return {
        type: "ADD_PRINT_STICKER",
        payload: stickers
    }
}

export const clearPrints = () => {
    return {
        type: "CLEAR_PRINTS"
    }
}

export const updateUserTurnament = (turnament: Tournament) => {
    return {
        type: "UPDATE_USER_TURNAMENT",
        payload: turnament
    }
}

export const setSendingFailed = (failed: boolean) => {
    return {
        type: "SET_SENDING_FAILED",
        payload: failed
    }
}