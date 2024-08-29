import { AlertColor } from "@mui/material"
import { ERROR_MESSAGE } from "../Components/Common/Internationalization/i18n"
import { Member } from "../types/ResponseTypes"

export const setDrinks = (drinks: Array<Member>) => {
    return {
        type: "SET_DRINKS",
        payload: drinks
    }
}

export const setDrinkCategories = (categories: Array<string>) => {
    return {
        type: "SET_DRINK_CATEGORIES",
        payload: categories
    }
}

export const setFavorites = (favorites: Array<string>) => {
    return {
        type: "SET_FAVORITES",
        payload: favorites
    }
}

export const setHistory = (history: Array<string>) => {
    return {
        type: "SET_HISTORY",
        payload: history
    }
}

export const setLoginState = (isLoggedIn: boolean) => {
    return {
        type: "SET_LOGIN",
        payload: isLoggedIn
    }
}

export const openToast = (settings: {
    message: string,
    headline?: string,
    duration?: number,
    type?: AlertColor
}) => {
    return {
        type: "OPEN_TOAST",
        payload: settings
    }
}

export const closeToast = () => {
    return {
        type: "CLOSE_TOAST",
        payload: ""
    }
}

export const setTransferDialogOpen = (open: boolean) => {
    return {
        type: "SET_TRANSFER_DIALOG",
        payload: open
    }
}

export const setRequestDialogOpen = (open: boolean) => {
    return {
        type: "SET_REQUEST_DIALOG",
        payload: open
    }
}

export const openErrorToast = () => {
    return openToast({ message: ERROR_MESSAGE, type: "error" })
}