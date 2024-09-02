import { Button, FormControl, Paper, Stack, TextField, Typography } from '@mui/material';
import React, { useState } from 'react'
import { useDispatch } from 'react-redux';
import { useNavigate, useSearchParams } from 'react-router-dom'
import { openToast } from '../../../Actions/CommonAction';
import { FALSCHES_PASSWORT, FEHLER, LOGIN, NAME, PASSWORT } from '../Internationalization/i18n';
import Spacer from '../Spacer';
import { doGetRequest, doPostRequest } from '../StaticFunctions';
import style from './login.module.scss'

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
            if (value.code === 200) {
                const searchParam = searchParams.get("originalPath")
                const notNullSeachParam = searchParam !== null ? searchParam : "/";

                navigate(notNullSeachParam)
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
