import { Button, FormControl, Paper, Stack, TextField, Typography } from '@mui/material';
import React, { useState } from 'react'
import { useDispatch } from 'react-redux';
import { useNavigate, useSearchParams } from 'react-router-dom'
import { openToast } from '../../../Actions/CommonAction';
import { FALSCHES_PASSWORT, FEHLER, LOGIN, NAME, PASSWORT, WARNUNG } from '../Internationalization/i18n';
import Spacer from '../Spacer';
import { doGetRequest, doPostRequest } from '../StaticFunctions';
import style from './login.module.scss'
import { sha256 } from 'crypto-hash';
import { verifySignature } from '../StaticFunctionsTyped';
import { Verification } from '../../../types/ResponseTypes';

type Props = {}

const Login = (props: Props) => {

    const [searchParams,] = useSearchParams();
    const navigate = useNavigate();
    const dispatch = useDispatch();
    const [disableLoginButton, setdisableLoginButton] = useState(false)
    const [username, setusername] = useState("")
    const [password, setpassword] = useState("")

    const login = () => {
        setdisableLoginButton(true);
        (function () {
            var cookies = document.cookie.split("; ");
            for (var c = 0; c < cookies.length; c++) {
                var d = window.location.hostname.split(".");
                while (d.length > 0) {
                    var cookieBase = encodeURIComponent(cookies[c].split(";")[0].split("=")[0]) + '=; expires=Thu, 01-Jan-1970 00:00:01 GMT; domain=' + d.join('.') + ' ;path=';
                    var p = window.location.pathname.split('/');
                    document.cookie = cookieBase + '/';
                    while (p.length > 0) {
                        document.cookie = cookieBase + p.join('/');
                        p.pop();
                    };
                    d.shift();
                }
            }
        })();
        doPostRequest("login", { name: username, password: password }).then((value) => {
            console.log(value)
            if (value.code === 200) {
                const searchParam = searchParams.get("originalPath")
                const notNullSeachParam = searchParam !== null ? searchParam : "/";
                sha256(password).then((hash) => {
                    window.localStorage.setItem("pw", hash)
                })

                window.localStorage.setItem("validation", JSON.stringify(value.content))

                navigate(notNullSeachParam)
            } else if (value.code === 503) {
                sha256(password).then((hash) => {
                    if (window.localStorage.getItem("pw") === hash) {


                        //PW matched check if license is still valid

                        const verificationString: string | null = window.localStorage.getItem("validation")
                        const verification: Verification = verificationString !== null ? JSON.parse(verificationString) : null

                        if (verification !== null) {
                            if (verifySignature(verification.validUntil + verification.name, verification.signedValidation)) {
                                if (new Date(verification.validUntil) < new Date()) {
                                    dispatch(openToast({ message: "Lizenz ist abgelaufen, bitte verlängern", type: "error", headline: "Lizenz abgelaufen", duration: 10000 }))
                                } else {
                                    const searchParam = searchParams.get("originalPath")
                                    const notNullSeachParam = searchParam !== null ? searchParam : "/o/" + verification.name;
                                    navigate(notNullSeachParam)
                                    dispatch(openToast({ message: "Keine Internetverbindung, die Lokalen dateien werden geladen und bei bestehender Internetverbindung hochgeladen", type: "warning", headline: "Server nicht erreichbar", duration: 10000 }))
                                }
                            } else {
                                dispatch(openToast({ message: "Die Verifikationdatei wurde manipuliert, bitte versuche es später erneut mit Internetverbindung", type: "error", headline: "Server nicht erreichbar", duration: 10000 }))
                            }
                        } else {
                            dispatch(openToast({ message: "Kein Account lokal hinterlegt, probiere es später erneut mit Internetverbindung", type: "error", headline: "Server nicht erreichbar", duration: 10000 }))
                        }
                    } else {
                        dispatch(openToast({ message: "Kein Account lokal hinterlegt oder Passwort falsch, probiere es erneut oder später mit Internetverbindung", type: "error", headline: "Server nicht erreichbar", duration: 10000 }))
                    }
                })

            } else {
                dispatch(openToast({ message: FALSCHES_PASSWORT, type: "error", headline: FEHLER }))
            }
            setdisableLoginButton(false)
        })
    }

    const loginOidc = async () => {
        setdisableLoginButton(true)
        doGetRequest("start-oidc").then(value => {
            if (value.code === 200) {
                console.log(value.content)
                window.location.href = value.content
            }
            setdisableLoginButton(false)
        })
    }

    const oidcButton = () => {
        if (window.globalTS.OIDC_BUTTON_TEXT === null) {
            return
        }

        return <><Spacer vertical={20} />
            <Button
                size='large'
                variant='contained'
                onClick={() => {
                    loginOidc()
                }}
                disabled={disableLoginButton}
            >
                {window.globalTS.OIDC_BUTTON_TEXT}
            </Button></>
    }

    return (<>
        <Spacer vertical={20} />
        <Stack direction="column" alignItems="center">
            <Paper className={style.paper}>
                <Stack direction="column" gap={3} alignItems="center">
                    <Typography variant="h3">DogDog Zeitmessung</Typography>
                    <Typography variant="h5">Bitte melde dich an</Typography>
                    <img
                        src={`/Logo.svg`}
                        width={"50%"}
                        alt='Logo'
                        loading="lazy"
                    />
                    <form className={style.form} noValidate autoComplete="off" onSubmit={(event) => { event.preventDefault(); login() }}>
                        <FormControl className={style.form}>
                            <Stack direction="column" gap={3} style={{ "width": "100%" }}>
                                <TextField
                                    fullWidth
                                    label={NAME}
                                    value={username}
                                    onChange={(value) => { setusername(value.target.value) }}
                                    autoFocus
                                />


                                <TextField
                                    fullWidth
                                    label={PASSWORT}
                                    type="password"
                                    value={password}
                                    onChange={(value) => { setpassword(value.target.value) }}
                                />

                                <Button
                                    size='large'
                                    variant='contained'
                                    onClick={() => {
                                        login()
                                    }}
                                    disabled={disableLoginButton}
                                    type='submit'
                                >
                                    {LOGIN}
                                </Button>
                                {oidcButton()}
                            </Stack>
                        </FormControl>
                    </form>
                </Stack>
            </Paper>
        </Stack>
    </>
    )
}

export default Login
