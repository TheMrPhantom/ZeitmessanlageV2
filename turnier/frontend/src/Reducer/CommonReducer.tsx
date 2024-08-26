import { AlertColor } from "@mui/material"
import { Drink, Member, Transaction } from "../types/ResponseTypes"

const defaultAlertType: AlertColor = "success"

const initialState: CommonReducerType = {
    drinks: null,
    drinkCategories: null,
    members: null,
    favorites: null,
    history: null,
    isLoggedIn: false,
    toast: {
        open: false,
        duration: 4000,
        headline: undefined,
        message: "",
        type: defaultAlertType
    },
    transferDialogOpen: false,
    requestDialogOpen: false,
}

export type CommonReducerType = {
    drinks: Array<Drink> | null,
    drinkCategories: Array<string> | null,
    members: Array<Member> | null,
    favorites: Array<number> | null,
    history: Array<Transaction> | null,
    isLoggedIn: boolean,
    toast: {
        open: boolean,
        duration: number,
        headline: string | undefined,
        message: string,
        type: AlertColor
    },
    transferDialogOpen: boolean,
    requestDialogOpen: boolean,
}

const reducer = (state = initialState, { type, payload }: any) => {

    var newState = { ...state }
    switch (type) {
        case "SET_DRINKS":
            newState.drinks = payload
            return newState

        case "SET_MEMBERS":
            newState.members = payload
            return newState

        case "SET_DRINK_CATEGORIES":
            newState.drinkCategories = payload
            return newState

        case "SET_FAVORITES":
            newState.favorites = payload
            return newState

        case "SET_HISTORY":
            newState.history = payload
            return newState

        case "SET_LOGIN":
            newState.isLoggedIn = payload
            return newState

        case "OPEN_TOAST":
            newState.toast.open = true;
            newState.toast.message = payload.message;
            newState.toast.headline = payload.headline
            newState.toast.duration = payload.duration ? payload.duration : initialState.toast.duration
            newState.toast.type = payload.type ? payload.type : defaultAlertType
            return newState

        case "CLOSE_TOAST":
            newState.toast.open = false;
            return newState
        case "SET_TRANSFER_DIALOG":
            newState.transferDialogOpen = payload
            return newState
        case "SET_REQUEST_DIALOG":
            newState.requestDialogOpen = payload
            return newState
        default:
            return state
    }

}
export default reducer
